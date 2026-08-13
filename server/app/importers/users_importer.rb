# frozen_string_literal: true

# db/data/users.yaml → upsert household users by derived email.
# Re-seed updates the name. Password is set only when creating a new user.
class UsersImporter < ApplicationImporter
  imports User, by: :email_address

  def email_address
    "#{name.to_s.strip.downcase}@ferney.org"
  end

  def apply_attributes(record)
    record.name = name
    return unless record.new_record?

    password = ENV.fetch("SEED_USER_PASSWORD", "password")
    record.password = password
    record.password_confirmation = password
  end
end
