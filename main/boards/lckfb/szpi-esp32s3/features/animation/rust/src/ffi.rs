//! Only this module handles pointers. C++ owns and retains all passed buffers.
use crate::{clock, codec, render};
use core::{ptr, slice};

#[repr(C)]
pub struct Info {
    pub width: u16,
    pub height: u16,
    pub frames: u32,
}
#[repr(C)]
pub struct Frame {
    pub data: *const u8,
    pub size: usize,
}

// Safety contract for each exported pointer API: input must reference `size`
// readable bytes; output objects/buffers must be valid, writable and disjoint.
// Neither pointers nor borrowed slices are stored after the call returns.
#[no_mangle]
pub unsafe extern "C" fn dsh_animation_info(data: *const u8, size: usize, out: *mut Info) -> bool {
    if data.is_null() || out.is_null() || size > 16 * 1024 * 1024 {
        return false;
    }
    let Some(animation) = codec::Animation::parse(slice::from_raw_parts(data, size)) else {
        return false;
    };
    ptr::write(
        out,
        Info {
            width: animation.width,
            height: animation.height,
            frames: animation.frames,
        },
    );
    true
}

#[no_mangle]
pub unsafe extern "C" fn dsh_animation_frame(
    data: *const u8,
    size: usize,
    index: u32,
    out: *mut Frame,
) -> bool {
    if data.is_null() || out.is_null() || size > 16 * 1024 * 1024 {
        return false;
    }
    let Some(animation) = codec::Animation::parse(slice::from_raw_parts(data, size)) else {
        return false;
    };
    let Some(frame) = animation.frame(index) else {
        return false;
    };
    ptr::write(
        out,
        Frame {
            data: frame.as_ptr(),
            size: frame.len(),
        },
    );
    true
}

#[no_mangle]
pub unsafe extern "C" fn dsh_animation_apply(
    data: *const u8,
    size: usize,
    previous: *mut u8,
    delta: *const u8,
    pixels: *mut u8,
    count: usize,
) -> bool {
    if data.is_null()
        || previous.is_null()
        || delta.is_null()
        || pixels.is_null()
        || size > 16 * 1024 * 1024
        || count > 176 * 176
    {
        return false;
    }
    let Some(animation) = codec::Animation::parse(slice::from_raw_parts(data, size)) else {
        return false;
    };
    if count != usize::from(animation.width) * usize::from(animation.height) {
        return false;
    }
    let indices = slice::from_raw_parts_mut(previous, count);
    codec::apply_indices(
        indices,
        slice::from_raw_parts(delta, count),
        animation.palette().len() / 4,
    ) && render::rgb565a8(
        indices,
        usize::from(animation.width),
        usize::from(animation.height),
        animation.palette(),
        slice::from_raw_parts_mut(pixels, render::PIXELS * 3),
    )
}

#[no_mangle]
pub extern "C" fn dsh_animation_delay_ms(origin: i64, frame: u32, now: i64) -> u32 {
    clock::delay_ms(origin, frame, now)
}
