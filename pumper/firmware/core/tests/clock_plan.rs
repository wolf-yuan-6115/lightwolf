#[path = "../../src/clock_plan.rs"]
mod clock_plan;
use pumper_core::format::Format;

#[test]
fn only_active_pcm16_192k_gets_220mhz_and_all_states_keep_usb_margin() {
    use clock_plan::*;
    for format in [Format::Pcm16, Format::Pcm24] {
        for rate in [44100, 48000, 88200, 96000, 176400, 192000] {
            for streaming in [false, true] {
                let div = divider(streaming, rate, format);
                let hz = frequency(div);
                let boost = streaming && rate == 192000 && format == Format::Pcm16;
                let nominal = if !streaming { 150_000_000 } else { 180_000_000 };
                if boost {
                    assert_eq!(hz, 220_000_000);
                } else {
                    assert!(hz.abs_diff(nominal) < nominal / 100_000); // <10 ppm
                    assert!(hz < 181_000_000);
                }
                // Even the longest fractional-divider interval exceeds E12's
                // 1.1 * 48 MHz requirement. No transition selects CLK_REF.
                assert!(PLL_HZ / div.div_ceil(1 << 16) >= 52_800_000);
            }
        }
    }
    assert_eq!(
        frequency(divider(false, 192000, Format::Pcm16)),
        150_000_728
    );
    assert_eq!(frequency(divider(true, 96000, Format::Pcm24)), 179_999_001);
}
