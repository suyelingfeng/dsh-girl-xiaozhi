//! Render at native LCD image size using RGB565 plus an alpha plane. LVGL can
//! blend this directly, avoiding per-frame software scaling and RGB conversion.
pub const EDGE: usize = 176;
pub const PIXELS: usize = EDGE * EDGE;

pub fn rgb565a8(
    indices: &[u8],
    width: usize,
    height: usize,
    palette: &[u8],
    output: &mut [u8],
) -> bool {
    if width == 0
        || height == 0
        || width > EDGE
        || height > EDGE
        || indices.len() != width * height
        || output.len() != PIXELS * 3
        || palette.is_empty()
        || palette.len() > 1024
        || palette.len() % 4 != 0
    {
        return false;
    }
    let colors = palette.len() / 4;
    if indices.iter().any(|index| usize::from(*index) >= colors) {
        return false;
    }
    // Small lookups live on the task stack, rather than repeatedly reading
    // palette entries from flash for every output pixel.
    let mut rgb = [0u16; 256];
    let mut alpha = [0u8; 256];
    for (i, p) in palette.chunks_exact(4).enumerate() {
        rgb[i] =
            (u16::from(p[2] & 0xf8) << 8) | (u16::from(p[1] & 0xfc) << 3) | u16::from(p[0] >> 3);
        alpha[i] = p[3];
    }
    let mut columns = [0usize; EDGE];
    for (x, source) in columns.iter_mut().enumerate() {
        *source = x * width / EDGE;
    }
    let (color_plane, alpha_plane) = output.split_at_mut(PIXELS * 2);
    for (y, (row, mask)) in color_plane
        .chunks_exact_mut(EDGE * 2)
        .zip(alpha_plane.chunks_exact_mut(EDGE))
        .enumerate()
    {
        let offset = (y * height / EDGE) * width;
        for (x, (pixel, opacity)) in row.chunks_exact_mut(2).zip(mask).enumerate() {
            let index = usize::from(indices[offset + columns[x]]);
            pixel.copy_from_slice(&rgb[index].to_le_bytes());
            *opacity = alpha[index];
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_size_color_conversion_and_transparency() {
        let palette = [0, 0, 0, 0, 0, 0, 255, 255];
        let mut pixels = vec![0; PIXELS * 3];
        assert!(rgb565a8(&[0, 1], 2, 1, &palette, &mut pixels));
        assert_eq!(&pixels[0..2], &[0, 0]);
        assert_eq!(&pixels[EDGE..EDGE + 2], &[0, 0xf8]);
        assert_eq!(pixels[PIXELS * 2], 0);
        assert_eq!(pixels[PIXELS * 2 + EDGE / 2], 255);
        assert_eq!(pixels[PIXELS * 3 - 1], 255);
        assert!(!rgb565a8(&[2], 1, 1, &palette, &mut pixels));
    }
}
