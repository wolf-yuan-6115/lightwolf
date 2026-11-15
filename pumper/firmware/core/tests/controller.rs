use pumper_core::{config::*, controller::*, protocol::Status, storage::Store};
fn command(op: u8, p: &[u8]) -> [u8; 64] {
    let mut b = [0; 64];
    b[..4].copy_from_slice(&[80, 69, 1, op]);
    b[4..6].copy_from_slice(&123u16.to_le_bytes());
    b[6] = p.len() as u8;
    b[8..8 + p.len()].copy_from_slice(p);
    b
}
fn call(c: &mut Controller, op: u8, p: &[u8], now: u64) -> Reply {
    c.handle(
        &command(op, p),
        now,
        DeviceStatus {
            rate: 48000,
            bits: 16,
            ..Default::default()
        },
        &[0; 56],
    )
}
#[test]
fn preview_load_and_independent_saves() {
    let mut c = Controller::new(Store::default());
    let reply = call(&mut c, 2, &[], 0);
    assert_eq!(&reply.bytes[8..11], &[5, 1, 10]);
    let mut eq = Config::FLAT;
    eq.preamp = -4.0;
    assert_eq!(call(&mut c, 0x10, &eq.global(), 0).bytes[7], 0);
    assert!(c.eq_dirty());
    assert_eq!(c.store.mask(), 0);
    let reply = call(&mut c, 0x23, &[3], 0);
    let Effect::Flash(action) = reply.effect else {
        panic!()
    };
    let plan = c.store.prepare(action).unwrap();
    let reply = c.flash_complete(0x23, 123, action, Ok(plan.store));
    assert_eq!(reply.bytes[6], 8);
    assert!(!c.eq_dirty());
    let cross = Crossfeed {
        mode: 3,
        ..Crossfeed::DEFAULT
    };
    call(&mut c, 0x12, &cross.encode(), 0);
    assert_ne!(c.runtime.crossfeed, c.store.crossfeed);
    call(&mut c, 0x21, &[], 0);
    assert!(c.eq_dirty());
    call(&mut c, 0x22, &[3], 0);
    assert!(!c.eq_dirty());
    assert_eq!(c.runtime.crossfeed, cross);
    assert_eq!(c.runtime.eq.preamp, -4.0);
}
#[test]
fn saved_snapshot_does_not_clear_newer_live_edits() {
    let mut c = Controller::new(Store::default());
    let reply = call(&mut c, 0x23, &[0], 0);
    let Effect::Flash(action) = reply.effect else {
        panic!()
    };
    let plan = c.store.prepare(action).unwrap();
    let new = Config {
        preamp: -8.0,
        ..Config::FLAT
    };
    call(&mut c, 0x10, &new.global(), 0);
    c.flash_complete(0x23, 123, action, Ok(plan.store));
    assert!(c.eq_dirty());
    assert_eq!(c.runtime.eq.preamp, -8.0);
    assert_eq!(c.saved.preamp, 0.0);
}
#[test]
fn malformed_reports_and_subscriptions() {
    let mut c = Controller::new(Store::default());
    assert_eq!(
        call(&mut c, 0x04, &[10], 0).bytes[7],
        Status::InvalidIndex as u8
    );
    assert_eq!(
        call(&mut c, 0x11, &[], 0).bytes[7],
        Status::InvalidLength as u8
    );
    let mut b = command(1, &[]);
    b[7] = 1;
    assert_eq!(
        c.handle(&b, 0, Default::default(), &[0; 56]).bytes[7],
        Status::InvalidPacket as u8
    );
    assert_eq!(
        call(&mut c, 0x31, &[], 0).bytes[7],
        Status::InvalidPacket as u8
    );
    let epoch = c.meter.epoch;
    assert_eq!(call(&mut c, 0x30, &[20, 0, 0xe2, 4], 100).bytes[7], 0);
    assert!(c.meter.active(101));
    assert_ne!(c.meter.epoch, epoch);
    assert_eq!(
        call(&mut c, 0x35, &[], 0).bytes[7],
        Status::InvalidCommand as u8
    );
    assert!(matches!(
        call(&mut c, 0x34, &[], 0).effect,
        Effect::SpectrumStart
    ));
    assert_eq!(
        call(&mut c, 0x35, &[], 1_250_001).bytes[7],
        Status::InvalidCommand as u8
    );
}
#[test]
fn pinning_preserves_host_readback_and_mute() {
    let mut c = Controller::new(Store::default());
    c.volume = -25600;
    c.publish_controls();
    assert!(c.runtime.gains[0] < 0.01);
    let output = Output {
        flags: 16,
        ..Output::DEFAULT
    };
    call(&mut c, 0x13, &output.encode(), 0);
    assert_eq!(c.runtime.gains, [1.0; 2]);
    let reply = call(&mut c, 6, &[], 0);
    assert_eq!(u16le(&reply.bytes[8..]) as i16, -25600);
    c.mute = true;
    c.publish_controls();
    assert_eq!(c.runtime.gains, [0.0; 2]);
    assert_eq!(c.store.output.flags, 0);
}

#[test]
fn factory_reset_reboots_only_after_successful_completion() {
    let mut c = Controller::new(Store::default());
    let before = c.store.clone();
    let reply = call(&mut c, 0x42, &[], 0);
    let Effect::Flash(action) = reply.effect else {
        panic!("reset must be deferred")
    };
    let failed = c.flash_complete(0x42, 123, action, Err(pumper_core::storage::Error::Corrupt));
    assert_eq!(failed.bytes[7], Status::StorageError as u8);
    assert!(matches!(failed.effect, Effect::None));
    assert_eq!(c.store, before);
    assert!(!c.flash_pending);
    let reply = call(&mut c, 0x42, &[], 0);
    let Effect::Flash(action) = reply.effect else {
        panic!()
    };
    let completed = c.flash_complete(0x42, 123, action, Ok(Store::default()));
    assert_eq!(&completed.bytes[6..8], &[0, Status::Ok as u8]);
    assert!(matches!(completed.effect, Effect::Restart));
}

#[test]
fn firmware_release_matches_package_and_preserves_wire_protocol() {
    let (major, minor) = pumper_core::FIRMWARE_VERSION;
    assert_eq!(major.to_string(), env!("CARGO_PKG_VERSION_MAJOR"));
    assert_eq!(minor.to_string(), env!("CARGO_PKG_VERSION_MINOR"));
    assert_eq!(pumper_core::protocol::VERSION, 1);
}

#[test]
fn extended_usb_volume_readback_and_ten_ms_gain_ramp() {
    use pumper_core::{dsp::Dsp, format::RATES};
    for rate in RATES {
        for db in [-100.0f32, -99.5, -50.5, -50.0, -0.5, 0.0] {
            let mut c = Controller::new(Store::default());
            c.volume = (db * 256.0) as i16;
            c.publish_controls();
            let reply = call(&mut c, 6, &[], 0);
            assert_eq!(u16le(&reply.bytes[8..]) as i16, c.volume);
            let expected = 10.0f64.powf(f64::from(db) / 20.0) as f32;
            assert!((c.runtime.gains[0] - expected).abs() <= expected * 1e-6);
            let mut dsp = Dsp::new(rate, c.runtime);
            let mut first = [16384.0; 2];
            dsp.process(&mut first, false, |_| {});
            if db < 0.0 {
                assert!(first[0] > 16384.0 * expected && first[0] < 16384.0);
            }
            let mut ramp = vec![16384.0; ((rate / 100 - 1) * 2) as usize];
            dsp.process(&mut ramp, false, |_| {});
            let target = 16384.0 * expected;
            assert!((ramp[ramp.len() - 1] - target).abs() <= target * 1e-6);
            assert!(!c.eq_dirty());
            assert_eq!(c.store.mask(), 0); // host volume never creates saved profiles
        }
    }
}
