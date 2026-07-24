class Exposure < ApplicationRecord
  belongs_to :user

  validates :started_at, presence: true
  validates :duration_seconds, presence: true,
                               numericality: { only_integer: true, greater_than: 0 }

  scope :newest_first, -> { order(started_at: :desc, id: :desc) }

  def ended_at
    return if started_at.blank? || duration_seconds.blank?

    started_at + duration_seconds.seconds
  end

  def duration_mmss
    return "—" if duration_seconds.blank?

    mm, ss = duration_seconds.divmod(60)
    format("%d:%02d", mm, ss)
  end
end
