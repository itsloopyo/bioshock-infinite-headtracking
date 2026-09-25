#pragma once

namespace cameraunlock::ads {

// The shape of the transition into and out of aiming down sights.
//
// Head tracking and iron sights want different things from the camera. Tracking
// says the view is wherever you are looking; a sight picture says the view is
// down the barrel, because that is the only place the weapon's own reticle means
// anything. So the moment the sights start coming up the head pose comes off the
// camera and the frame settles onto the aim - which is where the reticle was
// already pointing, so the thing the player was about to shoot ends up in the
// middle of the screen.
//
// This class owns the SHAPE of that transition and nothing else. It returns a
// scale, 1 at the hip and 0 with the sights up, and the caller decides what the
// scale blends between:
//
//   AdsMode::Paused    blend the head pose down to nothing and hold it there.
//   Marker / Tracked   blend the absolute pose into the pose measured from the
//                      entry frame (entry_pose.h), which is identity at that
//                      moment.
//
// So all three modes make the same swing onto the aim, and differ only in what
// happens for the rest of the aim.
//
// It is a SUSPEND, not a reset. The pose keeps flowing through the pipeline with
// its smoothing state intact, so lowering the weapon eases the view back to
// where the head actually is. Resetting instead would swing the view back
// through the whole head angle on the way out, dozens of times a firefight.
// Reset stays right for menus, cinematics and the master toggle, which is what
// Reset() below is for.
//
// The tracker's centre is deliberately not moved by any of this. Head centre
// means "looking down the gun", always. Recentring on the sights coming up - the
// obvious first idea - makes the pose the player happened to hold when they
// pressed aim the new neutral, so they have to HOLD their head turned to keep
// looking where they shot, and every aim press walks the neutral further from
// where the head actually rests.
//
// Pure: no clock of its own, no logging, no game. nowMs comes from the caller,
// which is what lets the whole transition be driven frame by frame in a test.
class AdsFade {
public:
    // How long the transition takes when the sights start coming up. Short
    // enough to be done before there is a sight picture to look through - a
    // weapon's own raise animation is around a fifth of a second - and long
    // enough that the view leans onto the gun rather than snapping to it.
    static constexpr unsigned long long kLowerMs = 150;
    // And back when they drop. Longer, because nothing is waiting on it and a
    // slower return is the more comfortable half.
    static constexpr unsigned long long kRaiseMs = 250;

    // Called once per rendered frame, before the head pose is applied. `aiming`
    // is the ADS state for this frame, polled rather than latched. Returns the
    // scale to blend at: 1 at the hip, 0 with the sights up.
    float Update(bool aiming, unsigned long long nowMs) {
        const bool turnDown = aiming && (m_state == State::Hip || m_state == State::Raising);
        const bool turnUp = !aiming && (m_state == State::Lowering || m_state == State::Aiming);

        if (turnDown || turnUp) {
            // A reversal starts from WHERE THE TRANSITION IS, not from the end
            // the interrupted leg would have reached. Starting each leg at its
            // own endpoint steps the pose by however far the previous one had
            // travelled, and the worst case is the most common input there is:
            // a tap of the aim button releases a frame after it was pressed, so
            // the pose is 99.99% applied and the next frame removes all of it.
            // That is the jolt this class exists to remove, delivered by the
            // class itself.
            const float from = Current(nowMs);
            const float target = turnDown ? 0.0f : 1.0f;
            const float distance = target > from ? target - from : from - target;
            if (distance < kSettled) {
                m_state = turnDown ? State::Aiming : State::Hip;
                return target;
            }
            m_state = turnDown ? State::Lowering : State::Raising;
            m_from = from;
            m_target = target;
            // Scaled by the distance left to travel, so an interrupted leg
            // moves at the same RATE as a whole one rather than taking the full
            // time to cover a fraction of the distance.
            const float full = static_cast<float>(turnDown ? kLowerMs : kRaiseMs);
            m_durationMs = static_cast<unsigned long long>(full * distance);
            if (m_durationMs == 0) m_durationMs = 1;
            m_startMs = nowMs;
        }

        const float scale = Current(nowMs);
        if ((m_state == State::Lowering || m_state == State::Raising)
                && Elapsed(nowMs) >= m_durationMs) {
            m_state = (m_target == 0.0f) ? State::Aiming : State::Hip;
        }
        return scale;
    }

    // Drops back to hip state. Call wherever tracking is suppressed - menu,
    // loading, cinematic, master toggle, tracker dropout - so the next aim
    // starts clean.
    void Reset() {
        m_state = State::Hip;
        m_from = 1.0f;
        m_target = 1.0f;
    }

private:
    enum class State { Hip, Lowering, Aiming, Raising };

    // Below this the two ends of a leg are the same place and there is nothing
    // to travel.
    static constexpr float kSettled = 1e-6f;

    // Clamped at zero rather than allowed to wrap. nowMs is the caller's clock
    // and an unsigned subtraction across a clock that stepped backwards yields
    // an enormous elapsed, which settles the transition instantly - a snap, in
    // the one place this class exists to prevent one.
    unsigned long long Elapsed(unsigned long long nowMs) const {
        return nowMs > m_startMs ? nowMs - m_startMs : 0;
    }

    // Where the transition is right now, without advancing it.
    float Current(unsigned long long nowMs) const {
        if (m_state == State::Hip) return 1.0f;
        if (m_state == State::Aiming) return 0.0f;
        const unsigned long long elapsed = Elapsed(nowMs);
        if (elapsed >= m_durationMs) return m_target;
        return m_from + (m_target - m_from) * Ease(elapsed, m_durationMs);
    }

    // Smoothstep, so the transition leaves and arrives at rest instead of
    // starting and stopping with a visible corner.
    static float Ease(unsigned long long elapsed, unsigned long long duration) {
        const float t = static_cast<float>(elapsed) / static_cast<float>(duration);
        return t * t * (3.0f - 2.0f * t);
    }

    State m_state = State::Hip;
    unsigned long long m_startMs = 0;
    unsigned long long m_durationMs = kLowerMs;
    // The scale the current leg started from and is heading to. Held rather
    // than assumed, because a leg can start anywhere: see Update().
    float m_from = 1.0f;
    float m_target = 1.0f;
};

}  // namespace cameraunlock::ads
