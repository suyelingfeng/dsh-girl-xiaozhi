//! DSH animation runtime: pure codec/clock/motion modules plus a narrow C ABI.
#![cfg_attr(not(test), no_std)]
mod clock;
mod codec;
mod ffi;
mod motion;
mod render;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    extern "C" {
        fn abort() -> !;
    }
    // ESP-IDF records the fault and resets instead of leaving a task spinning.
    unsafe { abort() }
}
