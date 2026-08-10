# Vitiligo therapy mode — single E760M (6 bulbs)
#
# Source: Appendix D "E760M, MD66, MD666 EXPOSURE GUIDELINE TABLE for VITILIGO"
# Row set: One (1) Single E760 MASTER Device Only (6 Bulbs Total).
# Skin type not used; all patients share one schedule.
# Frequency: 2 times per week, never on consecutive days.

@e760m @therapy @vitiligo
Feature: Vitiligo dosing on single E760M master
  As a vitiligo patient prescribed home UVB-NB on one E760M panel
  I want the single-panel vitiligo EGT times
  So that depigmented lesions approach mild pink without solid burn

  Background:
    Given the system is one E760M MASTER with 6 bulbs and no ADD-ONs
    And the skin condition is vitiligo
    And skin type is not required for vitiligo EGT selection
    And treatments are planned 2 times per week
    And treatments are never on consecutive days
    And times are per body position

  Scenario: Initial and step times for single E760M vitiligo
    When starting vitiligo phototherapy on this system from the beginning
    Then the initial treatment time per body position is 0:50
    And that initial time corresponds to approximately 300 mJ/cm2
    And when increasing after no or minimal effect the step increase is 0:08 (50 mJ/cm2)
    And the listed maximum total time is 1:40 corresponding to approximately 600 mJ/cm2
    And the patient does not exceed that maximum unless directed under the vitiligo time discussion with a physician

  Scenario Outline: Next dose from lesion response within 3 days
    Given the previous vitiligo treatment was within 3 days
    And 12 to 24 hours after that treatment the vitiligo-affected skin result was "<result>"
    When planning the next body-position time
    Then the patient must "<action>"

    Examples:
      | result                                      | action                                              |
      | no or minimally noticeable effect           | increase previous time by 0:08                      |
      | light pink / very mild burn (sub-erythema)  | keep the same treatment time                        |
      | significant erythema (burns) red            | skip next 1 or 2 treatments and reduce time         |
      | significant erythema with edema or blisters | stop treatments and consult physician               |

  Scenario: Optimal vitiligo response is very mild lesion pink
    Then the optimal dose is when the depigmented white lesion reaches very mild erythema
    And 6 to 24 hours after treatment the lesion is ideally very light pink
    And the lesion must not be solid bright pink or red
    And the patient must never get burned

  Scenario: Multi-panel vitiligo rows are out of scope
    Then the 12-bulb and 18-or-more-bulb vitiligo EGT rows are not used
