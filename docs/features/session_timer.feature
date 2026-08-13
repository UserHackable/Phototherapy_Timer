# session_timer UI — behavioral spec (Gherkin)
#
# Implementation: esp32_firmware/apps/session_timer
# Lamp: SSR GPIO26 + blue LED GPIO2. Fan SSR GPIO27 (30 s after lamp off).
# Piezo GPIO25 end beep. Displays: LCD1602 + TM1637.
# Wi‑Fi + UDP JSON discovery; SNTP fallback.
# Key A: users list (household + Guest); digit 0–9: therapy → entry.
# A then user then B then therapy assigns in one sequence (A1B4 = user 1, Eczema).
# Keys typed during UDP fetches or the last-session hold are kept.
# Last exposure is the user's newest lamp-on, regardless of therapy mode change.
# Key B: therapy list; digit selects type; psoriasis then skin type 1–6.
# Key C: recommended + step_seconds; D: recommended − step (EGT / Manual 15 s / else 10 s).
# If recommended_seconds is 0 (too soon), C and D stay at 0.
# Key *: restore initial_seconds (EGT listed initial; Manual / none 30 s).
# Boot default entry: 30 seconds until a therapy reply. Exposure log on lamp off.
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

  Scenario: Star restores initial dose but keeps the selected user
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included initial_seconds 50
    And the user has entered digits "134"
    When the user presses "*"
    Then the programmed duration is 0 minutes and 50 seconds
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
    Then the lamp SSR output is on
    And the fan SSR output is on
    And the blue status LED is on
    And the LCD shows Warming for about 2 seconds while the tubes strike
    And then the session is running
    And the LED display shows remaining time starting at "00:45"
    And the logged duration is the countdown (dose), not the warmup

  Scenario: Abort during warmup does not log an exposure
    Given the user presses "#"
    And the lamps are still warming
    When the user presses "*"
    Then the lamp SSR output is off
    And no exposure is logged

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
    And the log includes therapy_id and skin_id from the last therapy reply when present
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

  Scenario: Key B requests the therapy list from the server
    Given the module has discovered the Rails server over UDP
    And a household user is selected (or Guest if none)
    When the user presses "B" in entry mode
    Then the module sends a JSON therapies request with its identity
    And the server replies with keypad therapies 1 Manual, 2 Psoriasis, 3 Vitiligo, 4 Eczema
    And the reply includes skin types 1–6
    And the LCD shows two therapies per page, one id:name per line
    And the TM1637 keeps showing wall clock HH:MM
    And pages advance every one second
    And after 30 seconds the UI returns to clock mode

  Scenario: A then 1 then B then 4 assigns Eczema and shows last session like A+digit
    Given household user 1 exists
    And user 1's last lamp-on was 2:00 with no therapy type
    When the user selects user 1 (A then 1) then eczema (B then 4)
    Then the module assigns therapy_id 4 (Eczema) to user 1
    And the LCD response is the same last-session screen as after A then 1
    And last_duration_seconds is 120 (2:00)
    And that 2:00 is the last time eczema uses
    And the message is Last session, not "No prior session"
    And if that last lamp-on were longer than eczema max, recommended would be the max once 44h have passed

  Scenario: Recommended time is capped at the selected therapy max
    Given the user selected household user "rob" via A then their id digit
    And the last lamp-on was 5:00
    And eczema is assigned with max_seconds 166
    And it has been at least 44 hours since that lamp-on
    When therapy is loaded
    Then last_duration_seconds is 300
    And recommended_seconds is 166
    And the entry is programmed to 2:46

  Scenario: After user is selected, B then 4 still uses that user's last lamp-on
    Given household user 1 is already selected via A then 1
    And the last session message showed 2:00
    When the user presses "B" then "4"
    Then eczema is assigned to user 1
    And the LCD shows Last session / 2:00 again
    And recommended_seconds follows the 44h rule from that 2:00 lamp-on

  Scenario: B then 1 assigns Manual and reloads recommendation
    Given the UI is paging the therapy list
    And Guest is the selected user
    When the user presses "1"
    Then the module assigns therapy_id 1 (Manual) to the selected user
    And it requests therapy for that user
    And the entry is programmed from recommended_seconds
    And step_seconds is 15 and initial_seconds is 30

  Scenario: B then 2 pages skin types for Psoriasis
    Given the UI is paging the therapy list
    When the user presses "2"
    Then the LCD pages skin types 1:Type I through 6:Type VI
    And no assignment is sent yet

  Scenario: B then 2 then 1 assigns Psoriasis Type I
    Given the UI is paging skin types after Psoriasis was chosen
    And household user "rob" is selected
    When the user presses "1"
    Then the module assigns therapy_id 2 and skin_id 1 to rob
    And it requests therapy for rob
    And recommended / step / max / initial come from psoriasis Type I

  Scenario: B during skin paging returns to the therapy list
    Given the UI is paging skin types
    When the user presses "B"
    Then the UI returns to the therapy list

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

  Scenario: Therapy reply message is shown on the LCD
    Given the UI is paging the household user list
    And the server will recommend 30 seconds for user 4
    And the therapy reply includes message "Last session 2d ago"
    When the user presses "4"
    Then the module sends a JSON therapy request with user_id 4
    And the LCD shows the therapy message (up to two lines of 16 characters)
    And after a short hold the UI returns to entry mode
    And the entry field is programmed to 0 minutes and 30 seconds
    And the LCD top line shows the selected user with the recommended time

  Scenario: Key C programs recommended plus step seconds
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included recommended_seconds 90 and step_seconds 16
    When the user presses "C" in entry mode
    Then the entry is programmed to recommended plus 16 seconds (1:46)
    And the LED display shows "01:46"
    And the LCD top line shows "rob" with "1:46" on the right
    And "#" will start that time

  Scenario: Key C with no prior exposure steps from the initial recommendation
    Given the user selected Guest via A then 0
    And the therapy reply included recommended_seconds 30, last_duration_seconds 0 and step_seconds 10
    When the user presses "C"
    Then the entry is programmed to 0 minutes and 40 seconds
    And the LED display shows "00:40"

  Scenario: Key C stays at zero when recommended is zero
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included recommended_seconds 0, last_duration_seconds 90 and step_seconds 16
    When the user presses "C"
    Then the entry is programmed to 0 minutes and 0 seconds
    And the LED display shows "00:00"
    And "#" will not start until a positive time is set

  Scenario: Key D programs recommended minus step seconds
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included recommended_seconds 90 and step_seconds 16
    When the user presses "D" in entry mode
    Then the entry is programmed to recommended minus 16 seconds (1:14)
    And the LED display shows "01:14"
    And the LCD top line shows "rob" with "1:14" on the right

  Scenario: Key D floors at zero when recommended is shorter than the step
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included recommended_seconds 10 and step_seconds 16
    When the user presses "D"
    Then the entry is programmed to 0 minutes and 0 seconds
    And the LED display shows "00:00"
    And "#" will not start until a positive time is set

  Scenario: Key D stays at zero when recommended is zero
    Given the user selected household user "rob" via A then their id digit
    And the therapy reply included recommended_seconds 0, last_duration_seconds 90 and step_seconds 16
    When the user presses "D"
    Then the entry is programmed to 0 minutes and 0 seconds
    And the LED display shows "00:00"

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
    And the LCD bottom line shows the calendar date with A or P (AM/PM) in the last column

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
      | B     | the therapy list is requested                       |
      | C     | recommended plus step_seconds is programmed         |
      | D     | recommended minus step_seconds is programmed        |

  # --- Network -------------------------------------------------------------

  Scenario: Wi-Fi and SNTP from NVS when available
    Given Wi-Fi credentials are stored in NVS
    When the device boots
    Then it attempts to connect and synchronize wall time

  Scenario: Server can poke the module to check for updates
    Given the module is listening on UDP 3000
    When the admin clicks "Check for update" on the device page
    Then the server sends {"type":"ota"} to the module
    And the module GETs /firmware/session_timer/manifest.json immediately
    And if the published version differs it installs and reboots
    And if it matches the LCD shows "Up to date" with the running version

  Scenario: UDP key inject sets the test flag then handles the key
    Given the module is listening on UDP 3000
    When the host sends {"v":1,"type":"key","keys":"A1B4"}
    Then the module remembers that host as the command peer
    And it sets test true immediately before each injected key
    And it handles those keys like the keypad (A then 1 then B then 4)
    And the UDP ack includes test true, the keys, and a status snapshot
    And later state changes are also sent as type status to that host
    And a real keypad press clears test first, then queues that key

  Scenario: Test mode does not energize the lamp SSR
    Given test is true from a UDP key inject
    When the session would turn the lamp on
    Then the lamp SSR GPIO26 stays off
    And status lamp is true
    And the fan, countdown, LCD, and piezo still run as a real session
    And an exposure UDP is sent with test true
    And the server does not store that exposure as a last session

  Scenario: UDP status check remembers the watcher and replies
    Given the module is listening on UDP 3000
    When the host sends {"v":1,"type":"watch"}
    Then the module remembers that host as the watcher
    And it replies with the current status snapshot
    And later state changes are sent as type status to that watcher

  Scenario: Unwatch stops status echoes
    Given the module is echoing status to this host
    When the host sends {"v":1,"type":"unwatch"}
    Then the module forgets that watcher
    And it stops sending status packets here
    And the ack has ok true and watching false

  Scenario: Module reports UI state and display text
    Given the module has discovered the Rails server over UDP
    When the LCD or LED content changes, or a discovery ping is sent
    Then the module sends JSON type "status" (or a ping with nested status)
    And the payload includes state, selected user, lcd two-line text, and led text
    And the server stores that snapshot on the Device
    And the module does not wait for a status reply

  Scenario: Offline falls back to timer-only
    Given network is unavailable
    When the device boots
    Then the timer UI still works
    And wall clock may show that network time is unavailable
    And the device retries network periodically

  # --- LAN OTA -------------------------------------------------------------

  Scenario: Idle module can install firmware from the LAN server
    Given the module has dual-OTA partitions from a prior USB flash
    And Wi-Fi is connected and a server IP is known
    And the UI is not in a running session
    And the server hosts /firmware/session_timer/manifest.json with a newer version
    When the module performs its periodic OTA check
    Then it downloads app.bin over HTTP
    And verifies the SHA-256 from the manifest
    And reboots into the new OTA slot

  Scenario: OTA is deferred while a session is running
    Given a session is running with the lamp on
    When an OTA check would otherwise run
    Then the module does not start an OTA download
    And the session continues uninterrupted

  # --- Safety defaults -----------------------------------------------------

  Scenario: Lamp defaults off at boot
    Given the device has just powered on or reset
    Then the SSR output is off
    And the blue status LED is off
