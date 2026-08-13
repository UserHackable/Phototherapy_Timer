# frozen_string_literal: true

# Guest is always ensured (id 0) for anonymous timer sessions.
# Household users and skin types come from db/data via data_imp (upsert, no wipe).
seed_password = ENV.fetch("SEED_USER_PASSWORD", "password")

guest = User.ensure_guest!(password: seed_password)
puts "  [0] user #{guest.name} <#{guest.email_address}> id=#{guest.id}"

DataImp.import +"users.yaml"
User.household.each do |user|
  puts "  [#{user.id}] user #{user.name} <#{user.email_address}>"
end
puts "Seeded guest + #{User.household.count} household users via data_imp from db/data/users.yaml"

DataImp.import +"skin_types.yml"
SkinType.ordered.each do |st|
  puts "  [#{st.number}] skin type #{st.roman} — #{st.description}"
end
puts "Seeded #{SkinType.count} skin types via data_imp from db/data/skin_types.yml"

DataImp.import +"therapy_types.yml"
TherapyType.ordered.each do |tt|
  puts "  [#{tt.slug}] #{tt.name} skin_type=#{tt.uses_skin_type?}"
end
puts "Seeded #{TherapyType.count} therapy types via data_imp from db/data/therapy_types.yml"
