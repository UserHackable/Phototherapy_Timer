# frozen_string_literal: true

# Load users from db/data/users.yaml (names in order).
# Emails are <name>@ferney.org. Passwords are set only here (not in the YAML).
# Guest (id 0) is always ensured for anonymous timer sessions.
require "yaml"

seed_password = ENV.fetch("SEED_USER_PASSWORD", "password")

guest = User.ensure_guest!(password: seed_password)
puts "  [0] user #{guest.name} <#{guest.email_address}> id=#{guest.id}"

users_path = Rails.root.join("db/data/users.yaml")
rows = YAML.safe_load_file(users_path, permitted_classes: [], aliases: false)
raise "db/data/users.yaml must be a list of user hashes" unless rows.is_a?(Array)

rows.each_with_index do |row, index|
  name = row.fetch("name").to_s.strip.downcase
  email = "#{name}@ferney.org"

  user = User.find_or_initialize_by(email_address: email)
  user.name = name
  # Only set password when creating so re-seeding does not reset real passwords.
  if user.new_record?
    user.password = seed_password
    user.password_confirmation = seed_password
  end
  user.save!
  puts "  [#{index + 1}] user #{user.name} <#{user.email_address}> id=#{user.id}"
end

puts "Seeded guest + #{rows.size} household users from #{users_path.relative_path_from(Rails.root)}"
