# UDP device discovery — behavioral spec (Gherkin)
#
# Implementation: server/app/services/udp_discovery_listener.rb
#                 esp32_firmware apps: session_timer, wifi_connect
# Protocol: docs/device-discovery.md
#
# Automated: server/test/services/udp_discovery_listener_test.rb
#            server/test/models/device_test.rb

Feature: LAN device discovery over UDP JSON
  As the phototherapy controller and Rails server
  We want the board to find the server, learn wall time, and list household users
  So that the timer can show clock and names without hard-coded server config

  Background:
    Given the Rails server listens on UDP port 3000
    And discovery is enabled (UDP_DISCOVERY is not "0")

  # --- Ping / pong ---------------------------------------------------------

  Scenario: Ping upserts device and returns pong with wall time
    Given an ESP32 with identity "esp32-b4bfe9e70e64"
    When it sends JSON {"v":1,"type":"ping","identity":"esp32-b4bfe9e70e64","app":"session_timer","version":"99eab52"} from IP 192.168.1.50
    Then a Device is created or updated with that identity and IP
    And the Device stores firmware_version "99eab52" and firmware_app "session_timer"
    And the server unicasts a JSON pong with identity, ip, unix, iso8601, tz, tz_offset, and tz_posix
    And the pong includes published_version from the OTA manifest when one exists
    And unix is a current Unix timestamp
    And the module applies tz_posix via setenv TZ before using local time

  Scenario: OTA poke is forwarded to the module
    Given a Device exists with identity "esp32-b4bfe9e70e64" and a LAN IP
    When the web app sends {"v":1,"type":"ota","identity":"esp32-b4bfe9e70e64"}
    Then the UDP listener unicasts type ota to that device IP on port 3000
    And replies ok true forwarded true

  Scenario: Status report stores LCD and LED snapshot
    Given a Device exists with identity "esp32-b4bfe9e70e64"
    When the ESP sends type status with lcd ["rob        0:29","* abort  Running"] and led "00:29"
    Then that Device last_status includes those display lines
    And last_status state is "running"
    And the server does not send a reply

  Scenario: Ping may include a nested status snapshot
    When a ping includes status state "clock" and lcd lines
    Then the Device last_status is updated
    And a pong is still sent

  Scenario: Same identity with new IP updates one device row
    Given a Device exists with identity "esp-abc" and ip "10.0.0.5"
    When a ping arrives for identity "esp-abc" from "10.0.0.9"
    Then that Device row now has ip "10.0.0.9"
    And no second row is created for the identity

  Scenario: Ping without identity still records by IP
    When a discovery upsert uses only ip "10.0.0.5"
    Then a Device exists with that IP and null identity

  Scenario: Pong packets are ignored
    When a pong JSON arrives at the server
    Then no Device is written
    And no reply is sent

  Scenario: Garbage payloads are ignored
    When a non-JSON or unknown payload arrives
    Then the listener does not raise
    And no Device is written

  # --- Users list (key A on session_timer) ---------------------------------

  Scenario: Users request returns id and name only
    Given household users exist in the database
    When the ESP sends {"v":1,"type":"users","identity":"esp32-…"}
    Then the server replies with type "users"
    And users is an array of at most 9 objects each with id and name
    And password_digest and email_address are not included
    And the sending device is upserted like a ping

  Scenario: Therapy request returns recommended exposure for a user
    Given household user 4 exists
    When the ESP sends {"v":1,"type":"therapy","identity":"esp32-…","user_id":4}
    Then the server replies with type "therapy"
    And user_id is 4
    And name matches that user
    And recommended_seconds is last duration after 44h, 0 if more recent, else initial_seconds
    And step_seconds is the user's EGT step, Manual 15, or 10 if none
    And max_seconds is the user's EGT listed max, Manual or none 1200
    And initial_seconds is the user's EGT listed initial, Manual 30, or 30 if none
    And last_duration_seconds is that user's newest exposure duration, or 0
    And therapy_id and skin_id are the user's current assignment keypad ids, omitted if none
    And the sending device is upserted like a ping

  Scenario: Therapy request for unknown user returns not_found
    When the ESP sends therapy for user_id 999999
    Then the reply includes error "not_found"
    And recommended_seconds is omitted

  Scenario: Exposure log creates a row for Guest or household user
    Given Guest user id 0 exists
    When the ESP sends type exposure with user_id 0, duration_seconds 30, and unix end time
    Then the server creates an Exposure for Guest
    And started_at is end time minus 30 seconds
    And the reply has ok true

  Scenario: Users list ends with Guest id 0
    When the ESP requests users
    Then the last entry is id 0 name Guest
    And household users appear before Guest

  Scenario: Therapy for Guest id 0 returns recommended exposure
    When the ESP sends therapy with user_id 0
    Then recommended_seconds is 30
    And step_seconds is 10
    And max_seconds is 1200
    And initial_seconds is 30
    And last_duration_seconds is Guest's newest exposure duration, or 0
    And name is Guest

  Scenario: Therapies request returns keypad therapy and skin lists
    When the ESP sends {"v":1,"type":"therapies","identity":"esp32-…"}
    Then the server replies with type "therapies"
    And therapies is 1 Manual, 2 Psoriasis, 3 Vitiligo, 4 Eczema
    And Psoriasis has uses_skin_type true
    And skin_types is 1–6 Type I–VI
    And the sending device is upserted like a ping

  Scenario: Assign therapy Manual to Guest
    When the ESP sends assign_therapy user_id 0 therapy_id 1
    Then the reply is ok true
    And Guest has a Manual assignment
    And later therapy requests use Manual step 15 and initial 30

  Scenario: Assign psoriasis without skin returns need_skin
    When the ESP sends assign_therapy user_id 4 therapy_id 2
    Then the reply is ok false error need_skin

  Scenario: Assign psoriasis with skin type updates the user
    When the ESP sends assign_therapy user_id 4 therapy_id 2 skin_id 1
    Then the reply is ok true
    And that user has Psoriasis — Type I
    And later therapy requests use Type I step / max / initial

  # --- Time preference (device side; contract with session_timer) ----------

  Scenario: Device prefers discovery time over SNTP
    Given a successful pong included unix
    When the session_timer applies wall time
    Then it uses the discovery unix value
    And SNTP is only used if discovery did not supply time
