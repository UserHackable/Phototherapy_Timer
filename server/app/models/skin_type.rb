class SkinType < ApplicationRecord
  has_many :user_therapies, dependent: :restrict_with_error

  validates :number, presence: true, uniqueness: true,
                     numericality: { only_integer: true, in: 1..6 }
  validates :roman, presence: true, uniqueness: true
  validates :description, presence: true
  validates :step_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true
  validates :max_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true
  validates :initial_seconds, numericality: { only_integer: true, greater_than: 0 }, allow_nil: true

  normalizes :roman, with: ->(r) { r.to_s.strip.upcase }
  normalizes :description, with: ->(d) { d.to_s.strip.gsub(/\s+/, " ") }

  scope :ordered, -> { order(:number) }

  def label
    "Type #{roman}"
  end

  def self.keypad_list
    ordered.map { |st| { id: st.number, name: st.label } }
  end
end

