# Stock Solarc MASTER timer behavior (manual Timer Characteristics)
#
# Source: Manual §13 Exposure Guidelines — Timer Characteristics;
# §14 Treatment Procedure (standard timer, not C01 clinic firmware).
#
# Not automated — contract for stock timer; custom session_timer should meet
# equivalent safety semantics where applicable.

@e760m @timer
Feature: Stock MASTER timer characteristics
  As a patient treating with the E760M standard timer
  I want predictable countdown, end signal, and retained settings
  So that each body-position exposure stops when the programmed time ends

  Background:
    Given the MASTER uses Solarc standard timer firmware
    And the timer is not the clinic "C01" firmware
    And the system is a single E760M MASTER with 6 bulbs

  Scenario: Display format is minutes and seconds
    When the timer shows a programmed or remaining time
    Then the display reads in "minutes:seconds"
    And an example of twelve minutes thirty-four seconds is shown as "12:34"

  Scenario: Maximum time setting is twenty minutes
    When the user attempts to set a treatment time
    Then the timer's maximum time setting is 20 minutes and 0 seconds
    And times above 20:00 cannot be programmed

  Scenario: End of countdown turns lights off and triple-beeps three times
    Given a treatment countdown is running
    When the remaining time reaches zero
    Then the ultraviolet lights turn off
    And the timer produces three audible triple-beeps
    And the display resets to the previous time setting
    And the user verifies that the lights are off at the sound of the beeps

  Scenario: Last time setting is retained after power removal
    Given the last treatment time setting was "1:23"
    When power is removed from the system for a long time
    And power is restored and the switchlock is turned ON
    Then the timer displays the last treatment time setting "1:23"

  Scenario: Power failure mid-treatment retains remaining time
    Given a treatment is running with remaining time "0:45"
    When a power failure occurs
    Then the timer retains the time remaining
    When power is restored and the user presses START/STOP
    Then the session resumes from the retained remaining time

  Scenario: Start and stop during a body-position exposure
    Given the timer is set to a treatment time and the patient is in position
    When the user presses START/STOP
    Then the lights turn on and the countdown begins
    When the user presses START/STOP again
    Then the lights turn off
    When the user presses START/STOP again
    Then the countdown resumes

  Scenario: Malfunction requires stopping use
    Given the timer behaves erratically
    Or the timer fails to turn off the lights at the end of the cycle
    Then the user must stop all treatments
    And contact Solarc Systems
    And must not continue using the system until resolved

  Scenario: Label safety note for unread manuals
    Then the MASTER device label states that initial exposure time per area should not exceed 10 seconds
    And that label note is for people who have not read the manual or mistake the unit for a tanning bed
