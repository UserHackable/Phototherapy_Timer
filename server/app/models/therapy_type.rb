class TherapyType < ApplicationRecord
  # Keypad digit (key B) → slug. Stable so adding a type later does not renumber.
  KEYPAD = {
    "manual" => 1,
    "psoriasis" => 2,
    "vitiligo" => 3,
    "atopic_dermatitis" => 4
  }.freeze

  has_many :user_therapies, dependent: :restrict_with_error
  has_many :users, through: :user_therapies

  validates :slug, presence: true, uniqueness: true
  validates :name, presence: true
  validates :description, presence: true
  validates :uses_skin_type, inclusion: { in: [ true, false ] }
  validates :step_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true
  validates :max_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true
  validates :initial_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true

  normalizes :slug, with: ->(s) { s.to_s.strip.downcase.tr(" ", "_") }
  normalizes :name, with: ->(n) { n.to_s.strip }
  normalizes :description, with: ->(d) { d.to_s.strip.gsub(/\s+/, " ") }

  scope :ordered, -> { order(:name) }

  def label
    name
  end

  def keypad_id
    KEYPAD[slug]
  end

  def keypad_label
    slug == "atopic_dermatitis" ? "Eczema" : name
  end

  def self.find_by_keypad_id(id)
    slug = KEYPAD.key(id.to_i)
    slug && find_by(slug: slug)
  end

  def self.keypad_list
    KEYPAD.sort_by { |_, digit| digit }.filter_map do |slug, digit|
      tt = find_by(slug: slug)
      next unless tt

      { id: digit, name: tt.keypad_label, uses_skin_type: tt.uses_skin_type? }
    end
  end
end
