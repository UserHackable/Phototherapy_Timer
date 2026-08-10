# Psoriasis long-term maintenance (§15)
#
# Source: Manual §15 Psoriasis Long Term Maintenance Program.
# Applies after clearing on single E760M (or any system); times stay at
# clearing dose while frequency is reduced.

@e760m @therapy @psoriasis @maintenance
Feature: Psoriasis long-term maintenance after clearing
  As a psoriasis patient who has cleared on the E760M
  I want a maintenance frequency plan that does not keep raising dose
  So that control is kept with lower cumulative lifetime UV dose

  Background:
    Given the patient achieved psoriasis clearing on the single E760M system
    And clearing means plaques are flattened with at least 95% improvement
    And the patient is no longer increasing exposure times for clearing

  Scenario: Mild course may pause therapy after clearing
    Given psoriasis returns only after several months without phototherapy
    Then waiting until return and restarting from the beginning may minimize lifetime dose
    And the physician is consulted for that strategy

  Scenario: Diminishing frequency maintenance
    Given a more severe course that needs ongoing control
    When starting maintenance
    Then the patient starts with the same treatment time used to achieve clearing
    And reduces the number of exposures per week by one for every two weeks of treatments
    And uses the same clearing treatment times unless significant erythema or return of psoriasis requires adjustment

  Scenario Outline: Maintenance problems
    Given the patient is on a maintenance schedule
    When "<problem>" occurs
    Then the patient should "<response>"

    Examples:
      | problem                         | response                                                                 |
      | significant erythema (burning)  | decrease each subsequent treatment time by 25% until erythema does not occur; then resume maintenance |
      | new areas of psoriasis          | increase times per EGT while keeping current weekly frequency; after clearing use new clearing time  |
      | psoriatic flare (>5% of sites)  | increase treatment frequency (e.g. +1 day/week for 2 weeks) then step back as control returns         |

  Scenario: Long gap during maintenance
    Given treatments have not been taken for 4 weeks
    Then erythema tolerance is significantly reduced
    And it may be necessary to restart from the beginning using the Exposure Guideline Table

  Scenario: Physician optimizes maintenance
    Then the physician helps determine the schedule that balances control and lifetime UV dose
