# Treatment procedure for standard timer (§14)
#
# Source: Manual §14 Treatment Procedure.
# Device: single E760M MASTER, 6 bulbs, no add-ons.

@e760m @procedure
Feature: UVB treatment procedure on single E760M
  As a household patient using the E760M master panel
  I want a consistent pre-check and per-body-position procedure
  So that each session follows the manufacturer safety steps

  Background:
    Given the system is a single E760M MASTER with 6 bulbs and no ADD-ONs
    And the patient has a treatment log and an applicable Exposure Guideline Table
    And physician examination (skin check) is planned at least once per year

  Scenario: Prepare from log and EGT before lighting
    Given the results of the previous treatment are known
    When the patient prepares for a new session
    Then the patient reviews the treatment log
    And prepares times according to the previous result and the specific Exposure Guideline Table

  Scenario: Cover sensitive and chronically sun-exposed skin
    Before starting ultraviolet exposure
    Then genitals are covered unless affected by disease
    And chronically sun-exposed areas such as face, head, shoulders, forearms, and upper chest are covered unless being treated
    And all persons exposed wear the supplied ultraviolet protective goggles

  Scenario: Household is warned and room is secured
    Before starting ultraviolet exposure
    Then others in the household are advised not to enter the room
    And parents or guardians diligently supervise persons in their care

  Scenario: Align devices to floor marks
    Given location marks were made on the floor during installation
    Before starting ultraviolet exposure
    Then the single MASTER is checked against those marks for correct position

  Scenario Outline: Full countdown cycle for one body position
    Given the switchlock is turned to ON
    And the timer displays the last treatment time setting
    When the user sets the timer to treatment time "<time>" for the current body position
    And double-checks the setting is not a factor-of-ten error
    And wears ultraviolet protective goggles
    And takes the body position
    And presses START/STOP
    Then the lights turn on and the timer counts down
    And the user immediately verifies the countdown is proper
    When the countdown completes
    Then the lights turn off
    And three triple-beeps sound
    And the display resets to the previous time setting

    Examples:
      | time  |
      | 0:50  |
      | 1:23  |

  Scenario: Pause mid-exposure
    Given a body-position countdown is running
    When the user presses START/STOP
    Then the lights turn off
    When the user presses START/STOP again
    Then the lights turn on and the countdown continues

  Scenario: Multiple body positions in one session
    Given the first body-position treatment has completed
    When another body position is required for coverage
    Then the user changes the time setting if required
    And repositions without significantly overlapping treated skin
    And presses START/STOP to begin the next body-position cycle
    And immediately checks that the timer is counting down properly

  Scenario: End of session
    When all required body positions for the session are complete
    Then the switchlock is turned OFF
    And the key is removed and hidden to prevent unauthorized use
    And goggles are stored with the system
    And treatment times and body positions are recorded in the treatment log
    And after about 24 hours the result of the treatment is recorded

  Scenario: Suspected malfunction during countdown
    Given a body-position countdown has started
    When the user suspects any timer or light malfunction
    Then the user leaves the room immediately
    And stops all treatments
    And contacts Solarc Systems
