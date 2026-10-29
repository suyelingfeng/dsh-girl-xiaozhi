//! Checked DSA1 parser and palette reconstruction. No allocations or platform API.
fn word(bytes: &[u8], offset: usize) -> Option<u16> {
    Some(u16::from_le_bytes(
        bytes.get(offset..offset + 2)?.try_into().ok()?,
    ))
}
fn dword(bytes: &[u8], offset: usize) -> Option<u32> {
    Some(u32::from_le_bytes(
        bytes.get(offset..offset + 4)?.try_into().ok()?,
    ))
}
pub struct Animation<'a> {
    pub data: &'a [u8],
    pub width: u16,
    pub height: u16,
    pub frames: u32,
    table: usize,
    payload: usize,
}
impl<'a> Animation<'a> {
    pub fn parse(data: &'a [u8]) -> Option<Self> {
        if data.get(..4)? != b"DSA1" || word(data, 8)? != 30 {
            return None;
        }
        let width = word(data, 4)?;
        let height = word(data, 6)?;
        let colors = word(data, 10)?;
        let frames = dword(data, 12)?;
        if !(1..=176).contains(&width)
            || !(1..=176).contains(&height)
            || !(1..=256).contains(&colors)
            || !(1..=1800).contains(&frames)
        {
            return None;
        }
        let table = 16 + usize::from(colors) * 4;
        let payload = table + (frames as usize + 1) * 4;
        if payload > data.len()
            || dword(data, table)? != 0
            || payload.checked_add(dword(data, table + frames as usize * 4)? as usize)?
                != data.len()
        {
            return None;
        }
        let animation = Self {
            data,
            width,
            height,
            frames,
            table,
            payload,
        };
        // Validate every compressed extent once, including frames not yet played.
        for index in 0..frames {
            animation.frame(index)?;
        }
        Some(animation)
    }
    pub fn frame(&self, index: u32) -> Option<&'a [u8]> {
        if index >= self.frames {
            return None;
        }
        let left = dword(self.data, self.table + index as usize * 4)? as usize;
        let right = dword(self.data, self.table + (index as usize + 1) * 4)? as usize;
        if left >= right {
            return None;
        }
        self.data
            .get(self.payload.checked_add(left)?..self.payload.checked_add(right)?)
    }
    pub fn palette(&self) -> &[u8] {
        &self.data[16..self.table]
    }
}
/// Validate a whole delta before mutating either output, avoiding partial frames.
pub fn apply_indices(previous: &mut [u8], delta: &[u8], colors: usize) -> bool {
    if previous.len() != delta.len()
        || colors == 0
        || colors > 256
        || previous
            .iter()
            .zip(delta)
            .any(|(a, b)| usize::from(a ^ b) >= colors)
    {
        return false;
    }
    for (index, change) in previous.iter_mut().zip(delta) {
        *index ^= change;
    }
    true
}

// Independent ARGB reference path used only to check transparent reconstruction.
#[cfg(test)]
pub fn apply_delta(previous: &mut [u8], delta: &[u8], palette: &[u8], pixels: &mut [u8]) -> bool {
    if previous.len() != delta.len()
        || pixels.len() != previous.len() * 4
        || palette.is_empty()
        || palette.len() % 4 != 0
    {
        return false;
    }
    let colors = palette.len() / 4;
    if previous
        .iter()
        .zip(delta)
        .any(|(a, b)| usize::from(a ^ b) >= colors)
    {
        return false;
    }
    for ((index, change), pixel) in previous
        .iter_mut()
        .zip(delta)
        .zip(pixels.chunks_exact_mut(4))
    {
        *index ^= change;
        let offset = usize::from(*index) * 4;
        pixel.copy_from_slice(&palette[offset..offset + 4]);
    }
    true
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn motion_erases_transparent_pixels_without_trails() {
        let palette = [0, 0, 0, 0, 20, 40, 60, 255];
        let mut indices = [0, 0];
        let mut pixels = [0; 8];
        assert!(apply_delta(&mut indices, &[1, 0], &palette, &mut pixels));
        assert_eq!(&pixels[..4], &palette[4..]);
        assert!(apply_delta(&mut indices, &[1, 1], &palette, &mut pixels));
        assert_eq!(&pixels[..4], &[0, 0, 0, 0]);
        assert_eq!(&pixels[4..], &palette[4..]);
        let before = pixels;
        assert!(!apply_delta(&mut indices, &[9, 0], &palette, &mut pixels));
        assert_eq!(before, pixels);
    }
    #[test]
    fn rejects_truncated_and_unbounded_headers() {
        for size in 0..40 {
            assert!(Animation::parse(&[0; 40][..size]).is_none());
        }
        let mut header = [0; 28];
        header[..4].copy_from_slice(b"DSA1");
        header[4] = 1;
        header[6] = 1;
        header[8] = 30;
        header[10] = 1;
        header[12] = 1;
        assert!(Animation::parse(&header).is_none());
        header[24] = 1;
        let mut valid = header.to_vec();
        valid.push(0);
        let a = Animation::parse(&valid).unwrap();
        assert_eq!(a.frame(0), Some(&[0][..]));
        assert!(a.frame(1).is_none());
        valid[24] = 255;
        assert!(Animation::parse(&valid).is_none());
    }
}
