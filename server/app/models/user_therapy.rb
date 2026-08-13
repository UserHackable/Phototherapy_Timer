class UserTherapy < ApplicationRecord
  belongs_to :user
  belongs_to :therapy_type
  belongs_to :skin_type, optional: true

  validates :therapy_type_id, uniqueness: { scope: :user_id, message: "is already assigned to this user" }
  validate :skin_type_matches_therapy

  # Last assigned or edited wins (key B re-select bumps updated_at).
  scope :newest_first, -> { order(updated_at: :desc, created_at: :desc, id: :desc) }

  def label
    if skin_type
      "#{therapy_type.label} — #{skin_type.label}"
    else
      therapy_type.label
    end
  end

  # EGT duration in seconds (single E760M). Psoriasis uses the skin type;
  # other modes use the therapy type. Nil if nothing is configured.
  def step_seconds
    lookup_duration(:step_seconds)
  end

  def max_seconds
    lookup_duration(:max_seconds)
  end

  def initial_seconds
    lookup_duration(:initial_seconds)
  end

  private

  def lookup_duration(field)
    if therapy_type&.uses_skin_type?
      skin_type&.public_send(field)
    else
      therapy_type&.public_send(field)
    end
  end

  def skin_type_matches_therapy
    return if therapy_type.blank?

    if therapy_type.uses_skin_type? && skin_type.blank?
      errors.add(:skin_type, "is required for #{therapy_type.name}")
    end
  end
end
