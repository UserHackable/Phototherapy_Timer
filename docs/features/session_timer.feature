# session_timer UI — behavioral spec (Gherkin)
#
# Implementation: esp32_firmware/apps/session_timer
# Lamp: SSR GPIO26 + blue LED GPIO2. Fan SSR GPIO27 (30 s after lamp off).
# Piezo GPIO25 end beep. Displays: LCD1602 + TM1637.
# Wi‑Fi + UDP JSON discovery; SNTP fallback.
# Key A: users list (household + Guest); digit 0–9: therapy → entry.
# Default entry / therapy: 30 seconds. Exposure log on lamp off.
#
# Not automated yet — product contract (see docs/features/README.md).

Feature: Session timer entry and countdown
  As a household user running UV phototherapy sessions
  I want to enter a duration, start it, and see a clear countdown
  So that exposure stops when time is up without retyping every repeat

  Background:
    Given the session_timer firmware is running
    And the lamp SSR is on GPIO26
    And the fan SSR is on GPIO27
    And the blue status LED on GPIO2 mirrors the lamp only
    And the piezo is on GPIO25
    And the TM1637 shows time as MM:SS in timer modes
    And the keypad uses digits, "#" to start, and "*" to clear or abort
    And the LCD top line shows the selected user name left and duration right
    And when no user is selected the name is "Guest"

  # --- Selected user display -----------------------------------------------

  Scenario: Guest is shown until a user is selected
    Given no user has been selected via A and a digit
    And the UI is in entry mode
    Then the LCD top line starts with "Guest"
    And the programmed time appears on the top-right of the LCD

  Scenario: Selected user name stays on the LCD after therapy load
    Given the user selected household user "rob" via A then their id digit
    And recommended exposure was applied
    Then the LCD top line shows "rob" on the left
    And the recommended time on the top-right
    When the session is running
    Then the LCD top line still shows "rob" with remaining time on the right
    When the session completes
    Then the LCD top line still shows "rob" with the sticky time on the right

  Scenario: Star restores default time but keeps the selected user
    Given user "rob" is selected with a programmed time
    When the user presses "*"
    Then the programmed duration is 0 minutes and 30 seconds
    And the LCD top line still shows "rob"

  # --- Time entry (MMSS) ---------------------------------------------------

  Scenario: Default programmed duration is 30 seconds
    Given the UI is in entry mode after boot
    And the user has not entered digits yet
    Then the programmed duration is 0 minutes and 30 seconds
    And the LED display shows "00:30"
    And the LCD top-right shows "0:30"

  Scenario Outline: Digit entry is interpreted as MMSS
    Given the UI is in entry mode
    And the entry field is empty or sticky for a fresh digit
    When the user enters digits "<digits>"
    Then the programmed duration is <minutes> minute(s) and <seconds> second(s)
    And the LED display shows "<display>"

    Examples:
      | digits | minutes | seconds | display |
      | 45     | 0       | 45      | 00:45   |
      | 134    | 1       | 34      | 01:34   |
      | 100    | 1       | 0       | 01:00   |
      | 5      | 0       | 5       | 00:05   |

  Scenario: Clear entry with star restores 30-second default
    Given the user has entered digits "134"
    When the user presses "*"
    Then the programmed duration is 0 minutes and 30 seconds
    And the LED display shows "00:30"

  Scenario: Hash starts with default duration when user has not typed
    Given the UI shows the default 30-second entry
    When the user presses "#"
    Then the session is running
    And the LED display shows remaining time starting at "00:30"

  # --- Start and run -------------------------------------------------------

  Scenario: Hash starts the countdown and turns the lamp and fan on
    Given the user has entered digits "45"
    When the user presses "#"
    Then the session is running
    And the lamp SSR output is on
    And the fan SSR output is on
    And the blue status LED is on
    And the LED display shows remaining time starting at "00:45"

  Scenario: Countdown reaches zero, stops the lamp, and beeps
    Given the user has entered digits "5"
    And the user presses "#"
    When the remaining time reaches zero
    Then the session is no longer running
    And the lamp SSR output is off
    And the blue status LED is off
    And the fan SSR remains on
    And the piezo beeps briefly once
    And the module sends a UDP exposure log for the selected user (or Guest id 0)
    And the log includes duration_seconds equal to the lamp-on time and unix end time
    And the UI shows done with the last programmed time
    And "#" will repeat that time

  Scenario: Fan runs 30 seconds after the lamp turns off
    Given a session has just ended and the lamp is off
    And the fan is still on
    When 30 seconds pass without starting a new session
    Then the fan SSR turns off
    When a new session starts before the 30 seconds elapse
    Then the fan stays on and the rundown timer is cancelled

  Scenario: Key A requests the household user list from the server
    Given the module has discovered the Rails server over UDP
    And the server has seeded users
    When the user presses "A" in entry mode
    Then the module sends a JSON users request with its identity
    And the server replies with household users then Guest id 0 last
    And the LCD briefly shows a fetch status then the name list
    And the LCD shows two users per page, one id:name per line
    And "0:Guest" appears on the last page of the list
    And a sparse last page leaves the bottom LCD line blank
    And the TM1637 keeps showing wall clock HH:MM (not page numbers)
    And pages advance every one second
    And after 30 seconds the UI returns to clock mode

  Scenario: A then 0 selects Guest and loads recommended exposure
    Given the UI is paging the household user list including Guest
    When the user presses "0"
    Then the module sends a therapy request with user_id 0
    And the entry is programmed from recommended_seconds
    And the LCD top line shows "Guest" with the time on the right

  Scenario: Key A retries discovery when the server is unknown
    Given discovery has not succeeded
    When the user presses "A"
    Then the module retries UDP discovery
    And then requests the user list if a server responds

  Scenario: Key A fetch failure stays out of users mode
    Given discovery has a server but the users request times out
    When the user presses "A"
    Then the LCD shows a users failure message
    And the UI remains in entry mode

  Scenario: After user list, digit selects user and loads recommended exposure
    Given the UI is paging the household user list
    And the server will recommend 30 seconds for user 4
    When the user presses "4"
    Then the module sends a JSON therapy request with user_id 4
    And the server replies with recommended_seconds 30
    And the entry field is programmed to 0 minutes and 30 seconds
    And the LED display shows "00:30"
    And the UI returns to entry mode ready to start with "#"

  Scenario: Digit for unknown user id does not load exposure
    Given the UI is paging the household user list
    And no listed user has id 9
    When the user presses "9"
    Then the module does not apply a new entry from therapy
    And the UI shows a brief failure or returns without changing sticky time

  Scenario: Star aborts a running session but keeps the last time
    Given a session is running with last programmed digits "130"
    When the user presses "*"
    Then the session stops
    And the SSR output is off
    And the blue status LED is off
    And the piezo does not beep
    And the last programmed duration remains available
    And the LED display shows "01:30"

  Scenario: Digits and hash are ignored while running
    Given a session is running
    When the user presses "1"
    And the user presses "#"
    Then the session continues running
    And the remaining time is not reset by those keys

  # --- After complete: repeat vs new entry ---------------------------------

  Scenario: Hash after complete repeats the same duration
    Given a session has completed with last programmed digits "45"
    And the SSR output is off
    When the user presses "#"
    Then a new session starts for 0 minutes and 45 seconds
    And the SSR output is on
    And the blue status LED is on

  Scenario: First digit after complete starts a fresh entry
    Given a session has completed with last programmed digits "134"
    When the user presses "4"
    Then the entry field is "4" only
    And the programmed duration is 0 minutes and 4 seconds
    And the LED display shows "00:04"

  Scenario: Further digits append after a fresh post-complete entry
    Given a session has completed with last programmed digits "134"
    When the user presses "4"
    And the user presses "5"
    Then the entry field is "45"
    And the programmed duration is 0 minutes and 45 seconds

  Scenario: Star after complete restores the 30-second default
    Given a session has completed with last programmed digits "45"
    When the user presses "*"
    Then the programmed duration is 0 minutes and 30 seconds
    And pressing "#" starts a 30-second session

  # --- Idle clock mode -----------------------------------------------------

  Scenario: One minute without input returns to clock mode
    Given the UI is in entry mode
    And wall time is available
    When no key is pressed for 60 seconds
    Then the UI is in clock mode
    And the TM1637 shows wall clock HH:MM
    And the LCD top line shows the selected user (or Guest) left and session time right
    And the LCD bottom line shows the calendar date

  Scenario: Idle timeout does not apply while counting down
    Given a session is running
    When 60 seconds pass without key input
    Then the session continues running
    And the UI does not enter clock mode

  Scenario: Stay on done screen until idle timeout
    Given a session has just completed
    When fewer than 60 seconds pass without key input
    Then the UI remains in entry mode showing done
    When no key is pressed for 60 seconds
    Then the UI is in clock mode

  Scenario: Requested time in clock mode is last successful or last entered
    Given the user last started or entered duration "1:34"
    And the UI is in clock mode
    Then the LCD top-right shows "1:34"
    When the user presses "#"
    Then a session starts for 1 minute and 34 seconds

  Scenario: Any key leaves clock mode
    Given the UI is in clock mode
    When the user presses a key
    Then the UI is no longer in clock mode

  Scenario Outline: Key behavior when leaving clock mode
    Given the UI is in clock mode
    And the sticky requested duration is "0:45"
    When the user presses "<key>"
    Then <result>

    Examples:
      | key   | result                                              |
      | #     | a session starts for 0 minutes and 45 seconds       |
      | *     | the entry field is cleared                          |
      | 1     | a new entry begins with digit 1                     |
      | A     | the entry UI is shown without changing sticky time  |

  # --- Network -------------------------------------------------------------

  Scenario: Wi-Fi and SNTP from NVS when available
    Given Wi-Fi credentials are stored in NVS
    When the device boots
    Then it attempts to connect and synchronize wall time

  Scenario: Offline falls back to timer-only
    Given network is unavailable
    When the device boots
    Then the timer UI still works
    And wall clock may show that network time is unavailable
    And the device retries network periodically

  # --- Safety defaults -----------------------------------------------------

  Scenario: Lamp defaults off at boot
    Given the device has just powered on or reset
    Then the SSR output is off
    And the blue status LED is off
