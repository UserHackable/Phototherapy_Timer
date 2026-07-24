# Exposure log — behavioral spec (Gherkin)
#
# Implementation: server/app/models/exposure.rb
#                 server/app/controllers/exposures_controller.rb
# Nested under users: /users/:user_id/exposures
#
# Automated: server/test/models/exposure_test.rb
#            server/test/controllers/exposures_controller_test.rb
# This file is the product contract for session/light-on history.

Feature: Per-user UV exposure log
  As a household admin
  I want each phototherapy run recorded against a user with start time and light-on duration
  So that we can review who was treated, when, and for how long

  Background:
    Given the Rails server is running
    And household users exist (seeded or created)
    And I am signed in

  # --- Model / fields ------------------------------------------------------

  Scenario: An exposure belongs to one user
    Given user "rob" has id 4
    When an exposure is created for user 4 with started_at "2026-07-24 10:00:00" and duration_seconds 90
    Then the exposure is stored under /users/4/exposures
    And ended_at is started_at plus 90 seconds
    And duration displays as "1:30"

  Scenario: Duration must be a positive integer
    Given I am creating an exposure for a user
    When I submit duration_seconds 0
    Then the exposure is not saved
    And I see a validation error on duration

  Scenario: Started_at is required
    Given I am creating an exposure for a user
    When I omit started_at
    Then the exposure is not saved

  # --- Nested routes -------------------------------------------------------

  Scenario: List exposures for a user
    Given user 4 has two exposures
    When I visit "/users/4/exposures"
    Then I see those exposures newest first
    And each row shows started time, duration, and ended time

  Scenario: Show a single exposure
    Given user 4 has an exposure with id 12
    When I visit "/users/4/exposures/12"
    Then I see started_at, duration, and ended_at for that exposure
    And the page names the owning user

  Scenario: Create exposure under a user
    Given I am on "/users/4/exposures/new"
    When I submit started_at and a positive duration_seconds
    Then a new exposure is created for user 4
    And I am redirected to the exposure show page under user 4

  Scenario: Update an exposure
    Given an exposure exists at "/users/4/exposures/12"
    When I change duration_seconds to 120 and save
    Then the exposure duration is 120 seconds
    And I remain under the user 4 nest

  Scenario: Destroy an exposure
    Given an exposure exists at "/users/4/exposures/12"
    When I destroy it
    Then it is removed from the database
    And I return to "/users/4/exposures"

  Scenario: Nested id must belong to the user
    Given exposure 99 belongs to user 2
    When I visit "/users/4/exposures/99"
    Then the response is not found

  # --- Users navigation ----------------------------------------------------

  Scenario: Users index links to exposure logs
    When I visit "/users"
    Then I see each household user
    And each user links to their exposures path

  Scenario: User show links to exposure log
    When I visit "/users/4"
    Then I can open "/users/4/exposures"

  # --- Auth ----------------------------------------------------------------

  Scenario: Guests cannot view exposures
    Given I am signed out
    When I visit "/users/4/exposures"
    Then I am redirected to sign in

  # --- Cascade -------------------------------------------------------------

  Scenario: Destroying a user removes their exposures
    Given user 4 has exposures
    When that user is destroyed
    Then those exposure rows are deleted

  # --- Device auto-log (lamp off) ------------------------------------------

  Scenario: Session complete logs exposure over UDP
    Given the lamp was on for 30 seconds for user 4
    When the countdown reaches zero and the light turns off
    Then the module sends {"type":"exposure","user_id":4,"duration_seconds":30,"unix":…}
    And the server creates an Exposure for user 4
    And started_at is unix end time minus duration

  Scenario: Abort also logs actual light-on duration
    Given a session was planned for 90 seconds for Guest
    And 20 seconds have elapsed
    When the user presses "*" and the light turns off
    Then an exposure is logged for user_id 0 with duration_seconds 20

  Scenario: Guest user has id 0
    Given the database is seeded
    Then a user exists with id 0 and name "Guest"
    And Guest is last in the UDP users list for key A then 0
