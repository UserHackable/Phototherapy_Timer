class Exposure < ApplicationRecord
  belongs_to :user
  belongs_to :therapy_type, optional: true
  belongs_to :skin_type, optional: true

  # If last session is this old or older, recommend its duration; else recommend 0.
  THERAPY_REUSE_AFTER = 44.hours

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

  def therapy_label
    return if therapy_type.blank?

    if skin_type
      "#{therapy_type.keypad_label} — #{skin_type.label}"
    else
      therapy_type.keypad_label
    end
  end

  def now
    Time.zone.now
  end

  def age
    ActiveSupport::Duration.build(now - started_at)
  end

  def age_seconds
    raise ArgumentError, "started_at required" if started_at.blank?

    total = (Time.zone.now - started_at).to_i
    total.negative? ? 0 : total
  end

  # Compact age for LCD: omit zero units (e.g. 10h 8m, 45m, 2d 3h).
  def ago_dhm
    total = age_seconds
    days = total / 86_400
    hours = (total % 86_400) / 3_600
    minutes = (total % 3_600) / 60
    parts = []
    parts << "#{days}d" if days.positive?
    parts << "#{hours}h" if hours.positive?
    parts << "#{minutes}m" if minutes.positive?
    return "just now" if parts.empty?

    parts.join(" ")
  end

  # Second LCD line: duration + age, max 16 chars.
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

    # Newest lamp-on for this user. Therapy type is ignored: a mode change
    # (or a session with no protocol) does not start a new last-exposure clock.
    def last_for(user)
      return if user.nil?

      where(user_id: user.id).newest_first.first
    end

    def last_session_message_for(user)
      exp = last_for(user)
      return "No prior session" if exp.nil?

      "Last session\n#{exp.last_session_detail_line}"
    end

    def last_duration_seconds_for(user)
      last_for(user)&.duration_seconds
    end

    def recommended_seconds_for(user, default_seconds: 30, max_seconds: nil)
      return default_seconds if user.nil?

      exp = last_for(user)
      return default_seconds if exp.nil?

      elapsed = (Time.zone.now - exp.started_at).to_f
      return 0 if elapsed < THERAPY_REUSE_AFTER.to_f

      dur = exp.duration_seconds
      if max_seconds.is_a?(Integer) && max_seconds.positive? && dur > max_seconds
        max_seconds
      else
        dur
      end
    end
  end
end
