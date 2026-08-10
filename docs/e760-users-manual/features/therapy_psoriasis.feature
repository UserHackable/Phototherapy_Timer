# Psoriasis therapy mode — single E760M (6 bulbs)
#
# Source: Appendix D "E760M, MD66, MD666 EXPOSURE GUIDELINE TABLE for PSORIASIS"
# Row set: One (1) Single E760 MASTER Device Only (6 Bulbs Total).
# Irradiance basis I=6 (nominal 6 mW/cm2). Frequency: 3 to 5 times per week.
# Never increase exposure times on consecutive days.

@e760m @therapy @psoriasis
Feature: Psoriasis dosing on single E760M master
  As a psoriasis patient prescribed home UVB-NB on one E760M panel
  I want starting times and step-ups by skin type for a 6-bulb single panel
  So that each body-position dose follows the manufacturer EGT for this system

  Background:
    Given the system is one E760M MASTER with 6 bulbs and no ADD-ONs
    And the skin condition is psoriasis
    And the physician has estimated the patient's skin type per manual Table 1
    And treatments are planned 3 to 5 times per week
    And times are per body position
    And the patient never increases exposure times on consecutive days
    And treatments on the same skin area are never repeated within 24 hours

  Scenario Outline: Initial and maximum treatment times by skin type (single E760M)
    Given the patient's skin type is <skin_type>
    When starting phototherapy on this system from the beginning
    Then the initial treatment time per body position is <initial>
    And that initial time corresponds to approximately <initial_dose>
    And when increasing after no or minimal effect the step increase is <step>
    And the maximum listed time is <max_time> corresponding to approximately <max_dose>

    Examples:
      | skin_type | initial | initial_dose | step  | max_time | max_dose     |
      | I         | 0:50    | 300 mJ/cm2   | 0:16  | 5:33     | 2000 mJ/cm2  |
      | II        | 0:50    | 300 mJ/cm2   | 0:16  | 5:33     | 2000 mJ/cm2  |
      | III       | 1:23    | 500 mJ/cm2   | 0:20  | 8:20     | 3000 mJ/cm2  |
      | IV        | 1:23    | 500 mJ/cm2   | 0:20  | 8:20     | 3000 mJ/cm2  |
      | V         | 2:13    | 800 mJ/cm2   | 0:25  | 13:53    | 5000 mJ/cm2  |
      | VI        | 2:13    | 800 mJ/cm2   | 0:25  | 13:53    | 5000 mJ/cm2  |

  Scenario Outline: Next dose from skin response within 3 days
    Given the previous psoriasis treatment was within 3 days
    And 12 to 24 hours after that treatment the skin result was "<result>"
    When planning the next body-position time
    Then the patient must "<action>"

    Examples:
      | result                                      | action                                              |
      | no or minimally noticeable effect           | increase previous time by the skin-type step        |
      | light pink / very mild burn (sub-erythema)  | keep the same treatment time                        |
      | significant erythema (burns) red            | skip next 1 or 2 treatments and reduce time         |
      | significant erythema with edema or blisters | stop treatments and consult physician               |

  Scenario: Sub-erythema is the target, not burn
    Then the maximum dosage goal is the slightest onset of mild pinkness in healthy skin
    And UVB phototherapy does not require sunburn
    And burning may worsen psoriasis

  Scenario: After clearing do not keep increasing time
    Given psoriasis has cleared (plaques flattened, >=95% improvement)
    Then the patient must not continue increasing exposure times
    And must switch to the psoriasis long-term maintenance program

  Scenario: Multi-panel EGT rows are out of scope
    Then the 12-bulb and 18-or-more-bulb rows of the psoriasis EGT are not used
