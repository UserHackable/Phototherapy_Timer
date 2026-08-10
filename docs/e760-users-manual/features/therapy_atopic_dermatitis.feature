# Atopic dermatitis (eczema) therapy mode — single E760M (6 bulbs)
#
# Source: Appendix D "E760M, MD66, MD666 EXPOSURE GUIDELINE TABLE for
# ATOPIC DERMATITIS (ECZEMA)"
# Row set: One (1) Single E760 MASTER Device Only (6 Bulbs Total).
# Skin type not used; all patients share one schedule.
# Frequency: 2 or 3 times per week, never on consecutive days.

@e760m @therapy @eczema @atopic-dermatitis
Feature: Atopic dermatitis (eczema) dosing on single E760M master
  As an atopic dermatitis patient prescribed home UVB-NB on one E760M panel
  I want the single-panel eczema EGT times
  So that dosing targets sub-erythema without burn

  Background:
    Given the system is one E760M MASTER with 6 bulbs and no ADD-ONs
    And the skin condition is atopic dermatitis (eczema)
    And skin type is not required for the eczema EGT selection
    And treatments are planned 2 or 3 times per week
    And treatments are never on consecutive days
    And times are per body position

  Scenario: Initial and step times for single E760M eczema
    When starting eczema phototherapy on this system from the beginning
    Then the initial treatment time per body position is 0:50
    And that initial time corresponds to approximately 300 mJ/cm2
    And when increasing after no or minimal effect the step increase is 0:16 (100 mJ/cm2)
    And the listed maximum total time is 2:46 corresponding to approximately 1000 mJ/cm2

  Scenario Outline: Next dose from skin response within 3 days
    Given the previous eczema treatment was within 3 days
    And 12 to 24 hours after that treatment the skin result was "<result>"
    When planning the next body-position time
    Then the patient must "<action>"

    Examples:
      | result                                      | action                                              |
      | no or minimally noticeable effect           | increase previous time by 0:16                      |
      | light pink / very mild burn (sub-erythema)  | keep the same treatment time                        |
      | significant erythema (burns) red            | skip next 1 or 2 treatments and reduce time         |
      | significant erythema with edema or blisters | stop treatments and consult physician               |

  Scenario: Multi-panel eczema rows are out of scope
    Then the 12-bulb and 18-or-more-bulb eczema EGT rows are not used
