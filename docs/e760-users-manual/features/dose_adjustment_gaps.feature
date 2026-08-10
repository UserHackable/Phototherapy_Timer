# Gap-since-last-treatment adjustments (EGT footnotes)
#
# Source: Appendix D EGT footnotes for E760 single-MASTER tables
# (psoriasis, vitiligo, atopic dermatitis). Apply only when last treatment
# was more than 3 days ago.

@e760m @therapy @gaps
Feature: Adjusting dose after a gap in treatments
  As a patient who missed days or weeks of UVB sessions
  I want clear gap rules from the E760 single-panel EGTs
  So that I reduce dose safely when tolerance may have fallen

  Background:
    Given the system is one E760M MASTER with 6 bulbs and no ADD-ONs
    And the previous treatment was more than 3 days ago

  # --- Psoriasis gap rules ---

  Scenario Outline: Psoriasis gap actions
    Given the therapy mode is psoriasis
    And the time since the last treatment is "<gap>"
    When planning the next exposure
    Then the patient must "<action>"

    Examples:
      | gap              | action                                      |
      | 4 to 7 days      | keep the same dose                          |
      | 1 to 2 weeks     | decrease dose by 25% or start over          |
      | 2 to 4 weeks     | decrease dose by 50% or start over          |
      | 4 or more weeks  | start over from the initial EGT time        |

  # --- Vitiligo gap rules ---

  Scenario Outline: Vitiligo gap actions
    Given the therapy mode is vitiligo
    And the time since the last treatment is "<gap>"
    When planning the next exposure
    Then the patient must "<action>"

    Examples:
      | gap              | action                                      |
      | 4 to 7 days      | keep the same dose                          |
      | 1 to 2 weeks     | decrease dose by 25% or start over          |
      | 2 to 3 weeks     | decrease dose by 50% or start over          |
      | 3 or more weeks  | start over                                  |

  # --- Atopic dermatitis gap rules ---

  Scenario Outline: Atopic dermatitis gap actions
    Given the therapy mode is atopic dermatitis (eczema)
    And the time since the last treatment is "<gap>"
    When planning the next exposure
    Then the patient must "<action>"

    Examples:
      | gap              | action                                      |
      | 4 to 7 days      | keep the same dose                          |
      | 1 to 2 weeks     | decrease dose by 50% or start over          |
      | 2 or more weeks  | start over                                  |

  Scenario: Always prefer lower time when uncertain
    When in doubt about the correct next exposure time
    Then the patient always uses the lower exposure time
    And the patient never aims to get burned

  Scenario: Same-area latency floor
    Then under no circumstances are treatments repeated on the same area within 24 hours
    And some patients with 48-hour latency treat at most every second day
