//! Absolute 30 Hz frame deadlines. Decode/render duration never accumulates drift.
pub fn delay_ms(origin_us: i64, frame: u32, now_us: i64) -> u32 {
    let deadline = origin_us.saturating_add(i64::from(frame) * 1_000_000 / 30);
    ((deadline.saturating_sub(now_us).max(0) + 999) / 1000).min(34) as u32
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn thirty_frames_span_one_second_without_rounding_drift() {
        assert_eq!(delay_ms(0, 1, 0), 34);
        assert_eq!(delay_ms(0, 2, 34_000), 33);
        assert_eq!(delay_ms(0, 30, 990_000), 10);
        assert_eq!(delay_ms(0, 30, 1_000_000), 0);
        assert_eq!(delay_ms(0, 30, 1_100_000), 0);
    }
}
