use pumper_core::{config::*, dsp::Dsp, format::RATES};
#[test]
fn complete_chain_matches_c_reference() {
    let mut bytes = include_bytes!("../../reference/golden/dsp.bin").chunks_exact(4);
    let mut sum = 0.0f64;
    let mut peak = 0.0f64;
    let mut count = 0;
    for rate in RATES {
        for mode in 0..36 {
            let mut r = Runtime::default();
            r.eq.preamp = -3.0;
            if mode < 7 {
                let b = &mut r.eq.bands[0];
                b.kind = mode;
                b.width = 0;
                b.frequency = 900.0;
                b.q = 0.7;
                b.gain = if mode <= 2 { 8.0 } else { 0.0 };
            } else {
                for (j, b) in r.eq.bands.iter_mut().enumerate() {
                    b.kind = 1;
                    b.width = 0;
                    b.frequency = 1000.0 * (j + 1) as f32;
                    b.q = 1.2;
                    b.gain = 3.0;
                }
            }
            if mode >= 8 {
                let kind = (mode - 8) % 7;
                let edge = (mode - 8) / 7;
                r.eq.preamp = if edge == 0 {
                    -241.0
                } else if edge == 1 {
                    12.0
                } else {
                    -3.0
                };
                let b = &mut r.eq.bands[0];
                b.kind = kind;
                b.width = u8::from(kind == 1 && edge >= 2);
                b.frequency = if edge & 1 != 0 { 20000.0 } else { 20.0 };
                b.q = if edge >= 2 {
                    if kind == 0 || kind == 2 { 1.0 } else { 20.0 }
                } else {
                    0.1
                };
                b.bw = if edge >= 2 { 4.0 } else { 0.1 };
                b.gain = if kind <= 2 {
                    if edge >= 2 { 24.0 } else { -24.0 }
                } else {
                    0.0
                };
                for b in &mut r.eq.bands[1..] {
                    b.gain = 0.0;
                }
            }
            r.crossfeed = Crossfeed {
                mode: mode % 5,
                strength: 3000,
                cutoff: 900,
                delay: 350,
            };
            r.output = Output {
                flags: mode % 16,
                balance: 1500,
                width: 13000,
            };
            if mode >= 8 {
                let edge = (mode - 8) / 7;
                r.crossfeed = Crossfeed {
                    mode: 4,
                    strength: if edge >= 2 { 4000 } else { 0 },
                    cutoff: if edge >= 2 { 2000 } else { 300 },
                    delay: if edge >= 2 { 600 } else { 0 },
                };
                r.output = Output {
                    flags: if edge & 1 != 0 { 16 } else { 3 },
                    balance: if edge & 1 != 0 { 10000 } else { -10000 },
                    width: if edge >= 2 { 20000 } else { 0 },
                };
            }
            r.gains = [0.5, 0.75];
            let mut dsp = Dsp::new(rate, r);
            let mut seed = 15u32;
            for block in 0..64 {
                if block == 16 {
                    r.eq.bands[0].frequency = if mode >= 8 {
                        if r.eq.bands[0].frequency < 100.0 {
                            21.0
                        } else {
                            19900.0
                        }
                    } else {
                        r.eq.bands[0].frequency * 1.01
                    };
                    assert!(dsp.set_config(r.eq));
                }
                if block == 32 {
                    r.crossfeed.mode = 4;
                    r.crossfeed.cutoff = 1300;
                    r.crossfeed.delay = 500;
                    r.output.flags ^= 2;
                    r.gains = [1.0; 2];
                    dsp.controls.configure(r.crossfeed, r.output, r.gains);
                }
                if block == 33 {
                    r.crossfeed.delay = 100;
                    dsp.controls.configure(r.crossfeed, r.output, r.gains);
                }
                let mut samples = [0.0; 128];
                for x in &mut samples {
                    seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                    *x = (seed >> 16) as i16 as f32 * 0.3;
                }
                let m = dsp.process(&mut samples, true, |_| {});
                assert_eq!(m.frames, 64);
                for actual in samples {
                    let expected = f32::from_le_bytes(bytes.next().unwrap().try_into().unwrap());
                    let err = (f64::from(actual) - f64::from(expected)) / 32768.0;
                    sum += err * err;
                    peak = peak.max(err.abs());
                    count += 1;
                }
            }
        }
    }
    // Linked-limiter attack/release over >50 ms at every rate, host mute ramps,
    // stream reset during a ramp, and rate changes against the C engine.
    for rate in RATES {
        let mut runtime = Runtime::default();
        let mut dsp = Dsp::new(rate, runtime);
        for block in 0..256 {
            if block == 160 || block == 200 {
                runtime.gains = [if block == 160 { 0.0 } else { 1.0 }; 2];
                dsp.controls
                    .configure(runtime.crossfeed, runtime.output, runtime.gains);
            }
            if block == 224 {
                dsp.reset_stream();
            }
            if block == 240 {
                dsp.set_rate(if rate == 48000 { 96000 } else { 48000 });
            }
            let mut samples = [0.0; 128];
            for p in samples.chunks_exact_mut(2) {
                p.copy_from_slice(if block == 0 {
                    &[65536.0, -32768.0]
                } else {
                    &[16384.0, -8192.0]
                });
            }
            dsp.process(&mut samples, true, |_| {});
            for actual in samples {
                let expected = f32::from_le_bytes(bytes.next().unwrap().try_into().unwrap());
                let error = (f64::from(actual) - f64::from(expected)) / 32768.0;
                sum += error * error;
                peak = peak.max(error.abs());
                count += 1;
            }
        }
    }
    assert!(bytes.next().is_none());
    let rms = (sum / count as f64).sqrt();
    eprintln!("C/Rust normalized RMS={rms}, peak={peak}");
    assert!(rms <= 1e-4 && peak <= 1e-3, "rms={rms} peak={peak}");
}
#[test]
fn bypass_exact_limiter_and_observer_order() {
    let mut dsp = Dsp::new(48000, Runtime::default());
    let mut samples = [0.0, 1.0, -32768.0, 32767.0, 1234.5, -100.25];
    let original = samples;
    let mut seen = Vec::new();
    let m = dsp.process(&mut samples, true, |p| seen.push(p));
    assert_eq!(samples, original);
    assert_eq!(m.limiter_frames, 0);
    assert_eq!(seen[1], [-32768.0, 32767.0]);
    let mut clip = [65536.0, -32768.0];
    let m = dsp.process(&mut clip, false, |p| {
        assert!(p[0] > 24576.0);
    });
    assert_eq!(m.limiter_frames, 1);
    assert_eq!(clip, [32767.0, -16384.0]);
    let mut nan = [f32::NAN, f32::INFINITY];
    dsp.process(&mut nan, false, |_| {});
    assert!(nan.iter().all(|x| x.is_finite()));
}
#[test]
fn records_and_invalid_updates_are_transactional() {
    let c = Config::FLAT;
    assert_eq!(Config::from_record(&c.record()), Some(c));
    assert!(c.valid());
    let mut bad = c.global();
    bad[0] = 2;
    assert!(c.with_global(&bad).is_err());
    for kind in 0..7 {
        let mut b = c.bands[0];
        b.kind = kind;
        b.width = 0;
        b.gain = 0.0;
        assert!(b.valid());
        let encoded = b.encode(3);
        assert_eq!(Band::decode(&encoded), Ok((3, b)));
    }
    assert!(Output::decode(&[0x80, 0, 0, 0, 0, 0, 0, 0]).is_none());
    assert!(Crossfeed::decode(&[0, 1, 0, 0, 0, 0, 0, 0]).is_none());
}

#[test]
fn limiter_releases_fully_after_clipping_then_silence_at_every_rate() {
    for rate in RATES {
        let mut dsp = Dsp::new(rate, Runtime::default());
        let mut overload = [65536.0, -32768.0];
        assert_eq!(dsp.process(&mut overload, true, |_| {}).limiter_frames, 1);
        // Two seconds greatly exceeds the 50 ms release and unity snap time.
        for _ in 0..rate * 2 / 64 {
            let mut silence = [0.0; 128];
            dsp.process(&mut silence, true, |_| {});
            assert!(silence.iter().all(|&v| v == 0.0));
        }
        let mut silence = [0.0; 128];
        let metrics = dsp.process(&mut silence, true, |_| {});
        assert_eq!(metrics.limiter_frames, 0, "limiter stuck at {rate} Hz");
        assert_eq!(metrics.encode(1)[28], 0, "LIM report stuck at {rate} Hz");
        let mut probe = [16384.0, -8192.0];
        let expected = probe;
        assert_eq!(dsp.process(&mut probe, true, |_| {}).limiter_frames, 0);
        assert_eq!(probe, expected, "gain not exactly unity at {rate} Hz");
    }
}

#[test]
fn limiter_keeps_immediate_linked_attack_and_fifty_ms_release() {
    use pumper_core::controls::Controls;
    for rate in RATES {
        let mut c = Controls::new(rate, Crossfeed::DEFAULT, Output::DEFAULT);
        for _ in 0..64 {
            let (out, limited) = c.process([65536.0, -32768.0]);
            assert!(limited, "steady overload at {rate} Hz");
            assert_eq!(out, [32767.0, -16384.0]);
        }
        let frames = (rate as f32 * 0.050).round() as usize;
        let mut out = [0.0; 2];
        for _ in 0..frames {
            (out, _) = c.process([16384.0, -8192.0]);
        }
        let expected_gain = 1.0 - 0.5 * (-1.0f32).exp();
        assert!(
            (out[0] / 16384.0 - expected_gain).abs() < 0.001,
            "release at {rate} Hz"
        );
        assert_eq!(out[1], -out[0] * 0.5);
        c.reset(rate);
        let (out, limited) = c.process([16384.0, -8192.0]);
        assert_eq!(out, [16384.0, -8192.0]);
        assert!(!limited);
    }
}
