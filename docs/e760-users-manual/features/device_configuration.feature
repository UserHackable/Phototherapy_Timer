# E760M single-panel device configuration
#
# Source: SolRx E-Series UVBNB User's Manual Rev 3.3A — Definitions,
# Specifications Tables 1–2, Exposure Guideline Tables (Appendix D).
# Configuration: one E760M MASTER, 6 bulbs, no ADD-ON panels.
#
# Not automated — manufacturer / household system contract.

@e760m @single-panel
Feature: E760M single MASTER system configuration
  As a household using one SolRx E760M master panel with six UVB-NB bulbs
  I want dosing and procedure rules scoped only to that arrangement
  So that multi-panel MD66/MD666 times are never applied by mistake

  Background:
    Given the phototherapy system is an "E760M-UVBNB" Type "M" MASTER for home use
    And the MASTER has exactly 6 UVB-Narrowband bulbs
    And no ADD-ON devices are connected
    And the system is not an MD66, MD666+, or multi-panel booth arrangement
    And the supply is the household 120 V configuration unless otherwise stated
    And the user has selected the Exposure Guideline Table for "One (1) Single E760 MASTER Device Only (6 Bulbs Total)"
    And all other EGT rows for 12-bulb or 18+-bulb systems are crossed out as not applicable

  Scenario: Single-panel E760M is the only dosing system
    Then the nominal UVB-Narrowband irradiance for 1 device is approximately 6 mW/cm2 at 10 inches
    And treatment times are listed per body position
    And multi-device rows for 12 and 18+ bulbs must not be used for dosing

  Scenario: Type M master includes the household timer and switchlock
    Then the MASTER provides the timer, switchlock, and power inlet
    And ADD-ON devices without a timer are not part of this configuration

  Scenario Outline: Therapy modes covered by manufacturer EGTs for this panel
    Given the patient's prescribed skin condition is "<condition>"
    Then the applicable Exposure Guideline Table is the E760 single-MASTER table for "<condition>"

    Examples:
      | condition                    |
      | psoriasis                    |
      | vitiligo                     |
      | atopic dermatitis (eczema)   |

  Scenario: Physician instructions override the manual
    Given the physician's written dosing differs from the Exposure Guideline Table
    Then the physician's instructions take priority over the User's Manual
