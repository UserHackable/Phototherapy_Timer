# Devices web UI — behavioral spec (Gherkin)
#
# Implementation: server/app/models/device.rb
#                 server/app/controllers/devices_controller.rb
#
# Automated: server/test/controllers/devices_controller_test.rb
#            server/test/models/device_test.rb

Feature: Device registry in the web UI
  As a household admin
  I want to see and manage ESP32 controller boards discovered on the LAN
  So that I know which modules are online and can fix bad records

  Background:
    Given I am signed in

  Scenario: List devices
    Given one or more Devices exist
    When I visit "/devices"
    Then I see each device with its IP and identity

  Scenario: Show a device
    When I visit a device show page
    Then I see its IP and identity

  Scenario: Create a device manually
    When I submit a new device with an IP
    Then a Device row is created
    And I am redirected to its show page

  Scenario: Update a device
    Given a device exists
    When I change its IP or identity
    Then the changes are saved

  Scenario: Destroy a device
    Given a device exists
    When I destroy it
    Then it is removed
    And I return to the devices index

  Scenario: Guests cannot manage devices
    Given I am signed out
    When I visit "/devices"
    Then I am redirected to sign in
