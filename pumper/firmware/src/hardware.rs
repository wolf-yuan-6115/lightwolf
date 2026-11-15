use core::sync::atomic::{AtomicU32, Ordering};
use embassy_rp::pac;
static CLOCK: AtomicU32 = AtomicU32::new(crate::clock_plan::PLL_HZ);

/// Reboot the whole RP2354 through its ROM watchdog path, including USB and core 1.
/// An ARM SYSRESETREQ is not the board-level reset used by the C firmware: on
/// hardware it left the USB host with an unresponsive configured device.
pub fn restart() -> ! {
    // REBOOT_TYPE_NORMAL (0) | NO_RETURN_ON_SUCCESS (0x100), 10 ms delay.
    // Silicon ROM is part of the hardware, not a linked Pico SDK dependency.
    let _ = embassy_rp::rom_data::reboot(0x100, 10, 0, 0);
    // A ROM rejection must leave a recoverable device, not resume normal playback.
    embassy_rp::rom_data::reset_to_usb_boot(0, 0);
    loop {
        cortex_m::asm::wfe();
    }
}

pub fn clock_hz() -> u32 {
    CLOCK.load(Ordering::Acquire)
}

/// Core 1 owns runtime changes, with DMA stopped; startup calls before core 1.
/// Keep PLL_SYS fixed: switching through 12 MHz CLK_REF while USB is active
/// violates RP2350-E12 and can lose control-transfer status/SETUP events.
pub fn set_clock(streaming: bool, rate: u32, format: pumper_core::format::Format) {
    let divider = crate::clock_plan::divider(streaming, rate, format);
    let hz = crate::clock_plan::frequency(divider);
    if hz == clock_hz() {
        return;
    }
    // RP2350 permits live SYS divider changes. One register write changes both
    // integer and fractional fields without a slow reference-clock interval.
    pac::CLOCKS.clk_sys_div().write(|w| w.0 = divider);
    // Sole runtime clock owner. USB/ADC/timer sources and PLL_SYS stay fixed;
    // PIO/DMA are quiesced, and PERI follows SYS.
    unsafe {
        embassy_rp::clocks::pumper_sys_clock_changed(hz);
    }
    CLOCK.store(hz, Ordering::Release);
}

pub fn serial() -> [u8; 16] {
    let mut info = [0u32; 9];
    // ROM writes at most the provided buffer length; CHIP_INFO is flag bit zero.
    let count = unsafe { embassy_rp::rom_data::get_sys_info(info.as_mut_ptr(), info.len(), 1) };
    assert_eq!(count, 4);
    let mut result = [0; 16];
    let mut id = [0; 8];
    id[..4].copy_from_slice(&info[2].to_le_bytes());
    id[4..].copy_from_slice(&info[3].to_le_bytes());
    for (i, byte) in id.iter().rev().enumerate() {
        result[i * 2] = b"0123456789ABCDEF"[usize::from(byte >> 4)];
        result[i * 2 + 1] = b"0123456789ABCDEF"[usize::from(byte & 15)];
    }
    result
}

pub fn led_init() {
    for pin in [9usize, 10] {
        pac::IO_BANK0.gpio(pin).ctrl().write(|w| w.set_funcsel(4));
        pac::PADS_BANK0.gpio(pin).modify(|w| {
            w.set_od(false);
            w.set_iso(false);
        });
    }
    for slice in [4usize, 5] {
        pac::PWM.ch(slice).top().write(|w| w.0 = 255);
        pac::PWM.ch(slice).div().write(|w| w.0 = 16);
        pac::PWM
            .ch(slice)
            .csr()
            .write(|w| w.0 = 1 | (1 << 2) | (1 << 3));
    }
}
pub fn leds(red: bool, peak: u32) {
    pac::PWM
        .ch(5)
        .cc()
        .write(|w| w.0 = if red { 102 } else { 0 });
    pac::PWM
        .ch(4)
        .cc()
        .write(|w| w.0 = (peak.min(32768) * 153 / 32768) << 16);
}
