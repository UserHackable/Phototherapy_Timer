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

  # Always "0d 9h 44m" style for the module second LCD line.
  def ago_dhm
    raise ArgumentError, "started_at required" if started_at.blank?

    total = (Time.zone.now - started_at).to_i
    total = 0 if total.negative?
    days = total / 86_400
    hours = (total % 86_400) / 3_600
    minutes = (total % 3_600) / 60
    "#{days}d #{hours}h #{minutes}m"
  end

  # Compact age (tests / logs). Prefer ago_dhm for LCD.
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

  # Second LCD line: duration + age, max 16 chars (HD44780 width).
  # Prefer "ago"; tighten "0d 9h 44m" -> "0d9h44m" when needed to fit.
  def last_session_detail_line
    age = ago_dhm
    candidates = [
      "#{duration_mmss} #{age} ago",
      "#{duration_mmss} #{age.delete(" ")} ago",
      "#{duration_mmss} #{age}",
      "#{duration_mmss} #{age.delete(" ")}"
    ]
    candidates.find { |s| s.length <= 16 } || candidates.first[0, 16]
  end

  class << self
    def latest
      newest_first.first
    end

    # Two-line therapy UDP `message` for session_timer 16x2 (\n splits lines).
    #   Last session
    #   1:30 0d 9h 44m ago
    def last_session_message_for(user)
      return "No prior session" if user.nil?

      exp = user.exposures.newest_first.first
      return "No prior session" if exp.nil?

      "Last session\n#{exp.last_session_detail_line}"
    end
  end
end
