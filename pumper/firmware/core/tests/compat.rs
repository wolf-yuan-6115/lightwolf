use pumper_core::{
    activity::Activity,
    feedback::Feedback,
    format::{self, Format, RATES},
    protocol::{self, Status},
    usb::{Host, HostDetection, Stream},
};

#[test]
fn c_reference_pcm_feedback_rounding_and_reports() {
    let reference = include_bytes!("../../reference/golden/compat.bin");
    let mut result = Vec::new();
    fn word(result: &mut Vec<u8>, value: u32) {
        result.extend_from_slice(&value.to_le_bytes());
    }
    for format in [Format::Pcm16, Format::Pcm24] {
        for rate in RATES {
            word(&mut result, u32::from(format.accepts(rate)));
        }
        let input: [u8; 768] = core::array::from_fn(|i| (i * 73 + 19) as u8);
        let mut samples = [0.0; 384];
        let mut packed = [0u32; 384];
        let frames = format::decode(&mut samples, &input, format).unwrap();
        word(&mut result, frames as u32);
        for value in &samples[..frames * 2] {
            word(&mut result, value.to_bits());
        }
        let count = format::pack(&mut packed, &samples[..frames * 2], format).unwrap();
        word(&mut result, count as u32);
        for value in &packed[..count] {
            word(&mut result, *value);
        }
        let boundaries = [
            -1e9,
            -32768.0,
            -32767.0,
            -2.0,
            -1.0,
            -2.0 / 3.0,
            0.0,
            2.0 / 3.0,
            1.0,
            2.0,
            32767.0,
            32768.0,
            1e9,
            0.0,
        ];
        let count = format::pack(&mut packed, &boundaries, format).unwrap();
        for value in &packed[..count] {
            word(&mut result, *value);
        }
    }
    for rate in RATES {
        let mut f = Feedback::new(rate);
        word(&mut result, f.nominal);
        for i in 0..256 {
            word(&mut result, f.update((i * 113) % 1200));
        }
    }
    for i in -200..=200 {
        word(&mut result, protocol::milli(i as f32 / 2000.0) as u32);
    }
    result
        .extend_from_slice(&protocol::response(0x37, 0x1234, Status::InvalidLength, &[]).unwrap());
    assert_eq!(result.as_slice(), reference);
}

#[test]
fn idle_clock_before_alternate_and_rejected_changes_are_atomic() {
    let mut s = Stream::default();
    assert!(s.set_alt(2));
    assert!(!s.set_rate(192000));
    assert_eq!(s.rate, 48000);
    assert!(s.set_alt(0));
    assert!(s.set_rate(192000));
    let generation = s.generation;
    assert!(!s.set_alt(2));
    assert_eq!(s.alternate, 0);
    assert_eq!(s.generation, generation);
    assert!(s.set_alt(1));
    assert!(s.set_rate(44100));
    assert!(s.set_alt(2));
    assert!(!s.set_alt(3));
    s.generation = u32::MAX;
    assert!(s.set_alt(0));
    assert_eq!(s.generation, 0);
}

#[test]
fn all_format_rates_and_host_feedback_variants() {
    for format in [Format::Pcm16, Format::Pcm24] {
        for rate in RATES {
            assert_eq!(
                format.accepts(rate),
                format == Format::Pcm16 || rate <= 96000
            );
        }
    }
    for (order, host) in [
        ([15, 2], Host::Linux),
        ([2, 15], Host::Windows),
        ([3, 15], Host::Macos),
        ([3, 2], Host::Macos),
    ] {
        let mut detection = HostDetection::default();
        detection.observe(1);
        detection.observe(order[0]);
        detection.observe(order[0]);
        detection.observe(order[1]);
        assert_eq!(detection.host(), host);
        detection.observe(1);
        assert_eq!(detection.host(), Host::Unknown);
    }
    let value = Feedback::new(44100).nominal;
    let (bytes, n) = Feedback::packet(value, true);
    assert_eq!(n, 3);
    assert_eq!(u32::from_le_bytes(bytes), value >> 2);
    let (bytes, n) = Feedback::packet(value, false);
    assert_eq!(n, 4);
    assert_eq!(u32::from_le_bytes(bytes), value);
}

#[test]
fn malformed_input_does_not_modify_output() {
    let mut out = [99.0; 4];
    assert!(format::decode(&mut out, &[0; 5], Format::Pcm16).is_err());
    assert_eq!(out, [99.0; 4]);
    assert!(format::decode(&mut out, &[0; 12], Format::Pcm16).is_err());
    assert_eq!(out, [99.0; 4]);
    for length in 0..70 {
        if length != 64 {
            assert!(protocol::Packet::decode(&vec![0; length]).is_err());
        }
    }
    let mut report = protocol::response(1, 2, Status::Ok, &[]).unwrap();
    report[6] = 57;
    assert!(protocol::Packet::decode(&report).is_err());
}

#[test]
fn led_pulses_coalesce_across_timer_wrap() {
    let mut a = Activity::default();
    let start = u32::MAX - 1000;
    assert!(a.level(start, true));
    a.notify(start);
    assert!(!a.level(start, true));
    a.notify(start.wrapping_add(1));
    assert!(a.level(start.wrapping_add(25000), true));
    assert!(!a.level(start.wrapping_add(50000), true));
    assert!(a.level(start.wrapping_add(75000), true));
    assert!(!a.level(0, false));
    assert!(a.level(1, true));
}

#[test]
fn descriptors_match_recorded_c_and_reserve_report_zero() {
    use pumper_core::descriptors::*;
    assert_eq!(
        DESC_DEVICE,
        include_bytes!("../../reference/desc_device.bin")
    );
    assert_eq!(
        DESC_CONFIGURATION,
        include_bytes!("../../reference/desc_configuration.bin")
    );
    assert_eq!(
        DESC_CONFIGURATION_MACOS,
        include_bytes!("../../reference/desc_configuration_macos.bin")
    );
    let mut pos = 0;
    let mut endpoints = Vec::new();
    while pos < DESC_CONFIGURATION.len() {
        let n = DESC_CONFIGURATION[pos] as usize;
        assert!(n >= 2);
        if DESC_CONFIGURATION[pos + 1] == 5 {
            endpoints.push(DESC_CONFIGURATION[pos + 2]);
        }
        pos += n;
    }
    assert_eq!(endpoints, [1, 0x81, 1, 0x81, 2, 0x82]);
    assert!(!DESC_HID_REPORT.contains(&0x85));
    let differing: Vec<_> = DESC_CONFIGURATION
        .iter()
        .zip(DESC_CONFIGURATION_MACOS)
        .enumerate()
        .filter(|(_, (a, b))| a != b)
        .collect();
    assert_eq!(differing.len(), 2);
    for (_, (normal, mac)) in differing {
        assert_eq!((*normal, *mac), (4, 3));
    }
}

#[test]
fn suspend_resume_preserves_alternate_but_reset_cancels_it() {
    use pumper_core::usb::Suspension;
    for alt in 0..=2 {
        let mut stream = Stream::default();
        stream.set_rate(96000);
        assert!(stream.set_alt(alt));
        let mut suspend = Suspension::default();
        let generation = stream.generation;
        suspend.update(&mut stream, true);
        assert_eq!(stream.alternate, 0);
        suspend.update(&mut stream, true);
        suspend.update(&mut stream, false);
        assert_eq!(stream.alternate, alt);
        assert_eq!(stream.rate, 96000);
        assert!(stream.generation > generation);
        suspend.update(&mut stream, true);
        suspend.cancel();
        stream.set_alt(0); // bus reset / unconfigure
        suspend.update(&mut stream, false);
        assert_eq!(stream.alternate, 0);
    }
}

#[test]
fn hid_admission_drops_bad_envelopes_and_prioritizes_busy_accounting() {
    use pumper_core::protocol::{Admission, admit};
    let good = protocol::response(2, 123, Status::Ok, &[]).unwrap();
    // A response opcode/status is still a valid envelope; dispatch rejects it.
    assert_eq!(admit(&good, false), Admission::Ready);
    for field in [0, 1, 2, 6] {
        let mut bad = good;
        bad[field] = 255;
        assert_eq!(admit(&bad, false), Admission::Malformed);
        assert_eq!(admit(&bad, true), Admission::Busy);
    }
    for length in 0..64 {
        assert_eq!(admit(&good[..length], false), Admission::Malformed);
    }
    assert_eq!(admit(&good, true), Admission::Busy);
}

#[test]
fn audio_controls_require_the_correct_recipient_interface_and_channel() {
    use pumper_core::usb::audio_control_address;
    for entity in [2u16, 4] {
        for selector in [1u16, 2] {
            let index = entity << 8;
            let value = selector << 8;
            assert_eq!(
                audio_control_address(true, index, value),
                Some((entity as u8, selector as u8))
            );
            assert_eq!(audio_control_address(false, index, value), None);
            for bad in 1..=255 {
                assert_eq!(audio_control_address(true, index | bad, value), None);
                assert_eq!(audio_control_address(true, index, value | bad), None);
            }
        }
    }
    for unknown_entity in [0, 1, 3, 5, 255] {
        assert_eq!(
            audio_control_address(true, unknown_entity << 8, 0x0100),
            None
        );
    }
}
