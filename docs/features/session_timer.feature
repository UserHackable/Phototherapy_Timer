# session_timer UI — behavioral spec (Gherkin)
#
# Implementation: esp32_firmware/apps/session_timer
# Lamp: SSR GPIO26 + blue LED GPIO2. Piezo GPIO25 end beep.
# Displays: LCD1602 + TM1637. Wi‑Fi/SNTP from NVS when available.
#
# Not automated yet — product contract for timer + idle clock.

Feature: Session timer entry and countdown
  As a household user running UV phototherapy sessions
  I want to enter a duration, start it, and see a clear countdown
  So that exposure stops when time is up without retyping every repeat

  Background:
    Given the session_timer firmware is running
    And the lamp SSR is on GPIO26
    And the blue status LED on GPIO2 mirrors the lamp
    And the piezo is on GPIO25
    And the TM1637 shows time as MM:SS in timer modes
    And the keypad uses digits, "#" to start, and "*" to clear or abort

  # --- Time entry (MMSS) ---------------------------------------------------

  Scenario Outline: Digit entry is interpreted as MMSS
    Given the UI is in entry mode
    And the entry field is empty
    When the user enters digits "<digits>"
    Then the programmed duration is <minutes> minute(s) and <seconds> second(s)
    And the LED display shows "<display>"

    Examples:
      | digits | minutes | seconds | display |
      | 45     | 0       | 45      | 00:45   |
      | 134    | 1       | 34      | 01:34   |
      | 100    | 1       | 0       | 01:00   |
      | 5      | 0       | 5       | 00:05   |

  Scenario: Clear entry with star
    Given the user has entered digits "134"
    When the user presses "*"
    Then the entry field is empty
    And the LED display shows "00:00"

  Scenario: Zero duration cannot start
    Given the entry field is empty
    When the user presses "#"
    Then the session does not start
    And the SSR output remains off
    And the blue status LED remains off

  # --- Start and run -------------------------------------------------------

  Scenario: Hash starts the countdown and turns the lamp on
    Given the user has entered digits "45"
    When the user presses "#"
    Then the session is running
    And the SSR output is on
    And the blue status LED is on
    And the LED display shows remaining time starting at "00:45"

  Scenario: Countdown reaches zero, stops the lamp, and beeps
    Given the user has entered digits "5"
    And the user presses "#"
    When the remaining time reaches zero
    Then the session is no longer running
    And the SSR output is off
    And the blue status LED is off
    And the piezo beeps briefly once
    And the UI shows done with the last programmed time
    And "#" will repeat that time

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

  Scenario: Star after complete clears the sticky last time
    Given a session has completed with last programmed digits "45"
    When the user presses "*"
    Then the entry field is empty
    And pressing "#" does not start a session until a positive duration is entered

  # --- Idle clock mode -----------------------------------------------------

  Scenario: One minute without input returns to clock mode
    Given the UI is in entry mode
    And wall time is available
    When no key is pressed for 60 seconds
    Then the UI is in clock mode
    And the TM1637 shows wall clock HH:MM
    And the LCD top line shows the date
    And the LCD bottom line shows wall clock on the left
    And the LCD bottom line shows the requested session time on the right

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
    Then the LCD bottom-right shows "1:34"
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
