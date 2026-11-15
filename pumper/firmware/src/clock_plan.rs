//! Fixed PLL and live SYS dividers avoid RP2350-E12 during USB operation.
use pumper_core::format::Format;

pub const PLL_HZ: u32 = 220_000_000;
const UNIT: u32 = 1 << 16;

pub const fn divider(streaming: bool, rate: u32, format: Format) -> u32 {
    let target = if !streaming {
        150_000_000
    } else if rate == 192_000 && matches!(format, Format::Pcm16) {
        PLL_HZ
    } else {
        180_000_000
    };
    ((PLL_HZ as u64 * UNIT as u64 + target as u64 / 2) / target as u64) as u32
}

pub const fn frequency(divider: u32) -> u32 {
    ((PLL_HZ as u64 * UNIT as u64) / divider as u64) as u32
}
