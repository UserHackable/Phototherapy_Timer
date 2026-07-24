class User < ApplicationRecord
  GUEST_ID = 0
  GUEST_NAME = "Guest"
  GUEST_EMAIL = "guest@ferney.org"

  has_secure_password
  has_many :sessions, dependent: :destroy
  has_many :exposures, dependent: :destroy

  validates :name, presence: true
  validates :email_address, presence: true

  normalizes :email_address, with: ->(e) { e.strip.downcase }
  normalizes :name, with: ->(n) { n.to_s.strip }

  scope :household, -> { where("id > 0").order(:id) }

  def guest?
    id == GUEST_ID
  end

  # Guest is id 0 (anonymous keypad sessions). Forced insert — AR often skips id 0.
  def self.ensure_guest!(password: "password")
    existing = find_by(id: GUEST_ID)
    if existing
      existing.update!(name: GUEST_NAME) if existing.name != GUEST_NAME
      return existing
    end

    now = Time.current
    insert({
      id: GUEST_ID,
      name: GUEST_NAME,
      email_address: GUEST_EMAIL,
      password_digest: BCrypt::Password.create(password),
      created_at: now,
      updated_at: now
    })
    find(GUEST_ID)
  end
end
