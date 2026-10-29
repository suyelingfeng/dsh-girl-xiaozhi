//! Time-based tilt smoothing and bounded UI publication.

/// C ABI state passed by value: no raw pointers or shared mutable Rust state.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MotionState {
    pub roll: f32,
    pub pitch: f32,
    pub time_us: i64,
}

/// 25 Hz visual updates; the sensor continues to sample at 50 Hz.
#[no_mangle]
pub extern "C" fn dsh_animation_ui_due(previous_us: i64, now_us: i64) -> bool {
    now_us.saturating_sub(previous_us) >= 40_000
}

/// Time-based low-pass smoothing, stable even after an application task stall.
/// Bound the target before filtering so large tilts do not delay the return.
#[no_mangle]
pub extern "C" fn dsh_animation_step(
    state: MotionState,
    roll: f32,
    pitch: f32,
    now_us: i64,
) -> MotionState {
    if !roll.is_finite() || !pitch.is_finite() {
        return state;
    }
    let elapsed = if state.time_us == 0 {
        40_000
    } else {
        now_us.saturating_sub(state.time_us).clamp(0, 200_000)
    };
    // Backward Euler RC filter: no libm dependency, bounded coefficient [0,1].
    let blend = elapsed as f32 / (80_000.0 + elapsed as f32);
    MotionState {
        roll: state.roll + (roll.clamp(-40.0, 40.0) - state.roll) * blend,
        pitch: state.pitch + (pitch.clamp(-40.0, 40.0) - state.pitch) * blend,
        time_us: now_us,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scheduling_handles_boundaries_and_clock_reversal() {
        assert!(!dsh_animation_ui_due(100_000, 139_999));
        assert!(dsh_animation_ui_due(100_000, 140_000));
        assert!(!dsh_animation_ui_due(100_000, 90_000));
    }

    #[test]
    fn smoothing_is_monotonic_bounded_and_returns_to_rest() {
        let mut state = MotionState::default();
        for i in 1..=50 {
            let next = dsh_animation_step(state, 30.0, -20.0, i * 40_000);
            assert!(next.roll >= state.roll && next.roll <= 30.0);
            assert!(next.pitch <= state.pitch && next.pitch >= -20.0);
            state = next;
        }
        assert!((state.roll - 30.0).abs() < 0.01);
        for i in 51..=100 {
            state = dsh_animation_step(state, 0.0, 0.0, i * 40_000);
        }
        assert!(state.roll.abs() < 0.01 && state.pitch.abs() < 0.01);
    }

    #[test]
    fn jitter_invalid_input_and_long_stalls_remain_stable() {
        let initial = dsh_animation_step(MotionState::default(), 180.0, -180.0, 40_000);
        assert!(initial.roll <= 40.0 && initial.pitch >= -40.0);
        let invalid = dsh_animation_step(initial, f32::NAN, 0.0, 80_000);
        assert_eq!(invalid.time_us, initial.time_us);
        let same_tick = dsh_animation_step(initial, 0.0, 0.0, 40_000);
        assert_eq!(same_tick.roll, initial.roll);
        let delayed = dsh_animation_step(initial, -40.0, 40.0, i64::MAX);
        assert!(delayed.roll.is_finite() && delayed.roll.abs() <= 40.0);
        assert!(delayed.pitch.is_finite() && delayed.pitch.abs() <= 40.0);
    }

    #[test]
    fn abi_layout_matches_cpp() {
        assert_eq!(core::mem::size_of::<MotionState>(), 16);
        assert_eq!(core::mem::offset_of!(MotionState, time_us), 8);
    }
}
