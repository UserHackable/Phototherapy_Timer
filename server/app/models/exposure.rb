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

  def now
    Time.zone.now
  end

  def age
    ActiveSupport::Duration.build(now - started_at)
  end

  # Compact age for module LCD therapy message (fits 16x2 with "Last session … ago").
  # Examples: "just now", "45m", "22h", "1d 22h", "3d", "2w".
  def ago_compact
    raise ArgumentError, "started_at required" if started_at.blank?

    total = (Time.zone.now - started_at).to_i
    total = 0 if total.negative?
    return "just now" if total < 60

    minutes = total / 60
    return "#{minutes}m" if minutes < 60

    hours = minutes / 60
    return "#{hours}h" if hours < 24

    days = hours / 24
    rem_h = hours % 24
    return "#{days}d #{rem_h}h" if days < 14 && rem_h.positive?
    return "#{days}d" if days < 60

    weeks = days / 7
    "#{weeks}w"
  end

  class << self
    def latest
      newest_first.first
    end

    # Free-text for therapy UDP reply `message` after A+digit user select.
    def last_session_message_for(user)
      return "No prior session" if user.nil?

      exp = user.exposures.newest_first.first
      return "No prior session" if exp.nil?

      label = exp.ago_compact
      label == "just now" ? "Last session just now" : "Last session #{label} ago"
    end
  end
end
