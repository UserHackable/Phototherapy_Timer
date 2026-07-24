# Authentication — behavioral spec (Gherkin)
#
# Implementation: server Rails 8 authentication generator
#   User, Session, Authentication concern, sessions/passwords controllers
#
# Automated: server/test/controllers/sessions_controller_test.rb
#            server/test/controllers/passwords_controller_test.rb
#            server/test/models/user_test.rb

Feature: Household sign-in
  As a household user
  I want to sign in with email and password
  So that device and exposure data stay private on the LAN web UI

  Background:
    Given a seeded user exists with email and password

  Scenario: Sign in with valid credentials
    When I post email and correct password to the session
    Then I am redirected to the app root
    And a signed session cookie is set

  Scenario: Sign in with invalid credentials
    When I post email and wrong password
    Then I remain unsigned in
    And I am sent back to the sign-in form

  Scenario: Sign out
    Given I am signed in
    When I delete the session
    Then the session cookie is cleared
    And I am sent to the sign-in form

  Scenario: Protected pages require authentication
    Given I am signed out
    When I visit a protected path such as "/devices" or "/users"
    Then I am redirected to sign in
    And after successful sign-in I can reach that path

  Scenario: User normalizes name and email
    When a user is saved with padded name and mixed-case email
    Then name is stripped
    And email is lowercased

  Scenario: Password reset request for known email
    When I request a password reset for a known email
    Then reset mail is enqueued
    And I see a generic "instructions sent" notice

  Scenario: Password reset request for unknown email
    When I request a password reset for an unknown email
    Then no mail is enqueued
    And I still see the same generic notice (no user enumeration)

  Scenario: Reset password with valid token
    Given I have a valid password reset token
    When I set a new matching password and confirmation
    Then my password digest changes
    And I can sign in with the new password

  Scenario: Reset rejected when passwords do not match
    Given I have a valid password reset token
    When password and confirmation differ
    Then the password digest is unchanged
