# Body positions for a single E760M panel (§12)
#
# Source: Manual §12 Body Positions.
# Single MASTER does not form a booth; multiple body positions are required
# for full 360° coverage.

@e760m @body-positions
Feature: Body positions for single E760M panel
  As a patient treating with one six-bulb master panel
  I want clear rules for distance and multi-position coverage
  So that I do not overexpose by standing too close or overlapping areas

  Background:
    Given the system is a single E760M MASTER panel that does not form a complete booth
    And exposure guideline times assume about 10 inches from the plane of the bulbs

  Scenario: Minimum exposure distance
    When taking any body position
    Then the minimum recommended exposure distance is 8 to 12 inches (20 to 30 cm) from the front of the bulbs
    And standing closer increases the chance of overexposure

  Scenario: Body position definition
    Then a body position is the posture held for one complete timer countdown cycle
    And during that cycle the patient does not spin; the body faces the system in a consistent direction

  Scenario: Multiple positions needed for full coverage
    Given only one MASTER panel provides limited angular coverage
    Then more than one body position is required for full 360 degree coverage
    And the number of positions depends on body size and the coverage of the single panel

  Scenario: No significant overlap between positions
    When using more than one body position in a session
    Then skin areas must not significantly overlap between positions
    And failure to avoid overlap may cause local overexposure and burns

  Scenario: Consistency of method
    When repeating sessions over time
    Then the patient uses a consistent sequence of body positions
    And a consistent distance from the bulbs
    And makes changes gradually over many sessions

  Scenario: Protect areas that should receive minimal UV
    Before exposure
    Then genitals are covered unless affected
    And chronically sun-exposed lifetime areas are covered unless intentionally treated

  Scenario: Irradiance peaks at panel center
    Then the maximum irradiance is at the lateral center of the device
    And the patient may move slightly during treatment to balance dose across skin areas
