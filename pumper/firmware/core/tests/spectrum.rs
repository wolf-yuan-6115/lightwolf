use pumper_core::{
    format::RATES,
    spectrum::{Analyzer, Pipe},
};
#[test]
fn matches_c_spectrum_all_rates_and_restarts_namespace() {
    let reference = include_bytes!("../../reference/golden/spectrum.bin");
    assert_eq!(reference.len(), 6 * 256);
    let pipe = Box::new(Pipe::new());
    let (mut capture, consumer) = pipe.split().unwrap();
    let mut analysis = Analyzer::new(consumer);
    let mut tick = 0u32;
    for (r, rate) in RATES.into_iter().enumerate() {
        analysis.start();
        assert!(capture.begin(rate, r as u32 + 1));
        capture.push([0.0; 2]);
        capture.end(1);
        for _ in 0..100 {
            analysis.slice(|| {
                tick += 1;
                tick
            });
        }
        let mut result = None;
        for offset in (0..rate / 3).step_by(128) {
            capture.begin(rate, r as u32 + 1);
            for i in 0..128 {
                let v = 0.5
                    * 32768.0
                    * ((core::f64::consts::TAU * 1000.0 * f64::from(offset + i) / f64::from(rate))
                        .sin() as f32);
                capture.push([v, -v]);
            }
            capture.end(2);
            for _ in 0..100 {
                analysis.slice(|| {
                    tick += 1;
                    tick
                });
            }
            if let Some(frame) = analysis.take() {
                result = Some(frame);
            }
        }
        let result = result.unwrap();
        assert_eq!(
            result.rate,
            if rate.is_multiple_of(44100) {
                44100
            } else {
                48000
            }
        );
        for (actual, expected) in result.levels.iter().zip(&reference[r * 256..r * 256 + 256]) {
            assert!(
                actual.abs_diff(*expected) <= 1,
                "rate {rate}: {actual} vs {expected}"
            );
        }
        assert!(result.sequence <= 7);
        let mut levels = Vec::new();
        for i in 0..6 {
            let (b, n) = result.chunk(i).unwrap();
            assert_eq!(n, if i == 5 { 48 } else { 56 });
            levels.extend_from_slice(&b[12..n]);
        }
        assert_eq!(levels, result.levels);
        analysis.stop();
    }
}
#[test]
fn reset_during_production_and_overflow_never_publish_old_history() {
    let pipe = Box::new(Pipe::new());
    let (mut p, c) = pipe.split().unwrap();
    assert!(pipe.split().is_none());
    let mut a = Analyzer::new(c);
    a.start();
    p.begin(48000, 1);
    for _ in 0..100 {
        p.push([1.0; 2]);
    }
    a.start();
    p.end(1);
    let mut t = 0;
    for _ in 0..10 {
        a.slice(|| {
            t += 1;
            t
        });
    }
    assert!(a.take().is_none());
    p.begin(44100, 2);
    for _ in 0..20000 {
        p.push([0.0; 2]);
    }
    p.end(2);
    assert!(pipe.dropped.load(core::sync::atomic::Ordering::Relaxed) > 0);
    a.stop();
    assert!(!p.begin(48000, 3));
}

fn render_tone(rate: u32, hz: f64, gain: f32, opposite: bool) -> pumper_core::spectrum::Frame {
    let pipe = Box::new(Pipe::new());
    let (mut capture, consumer) = pipe.split().unwrap();
    let mut analysis = Analyzer::new(consumer);
    let mut tick = 0u32;
    analysis.start();
    capture.begin(rate, 1);
    capture.push([0.0; 2]);
    capture.end(1);
    for _ in 0..100 {
        analysis.slice(|| {
            tick += 1;
            tick
        });
    }
    let mut result = None;
    for offset in (0..rate / 3).step_by(128) {
        capture.begin(rate, 1);
        for i in 0..128 {
            let v = gain
                * 32768.0
                * (core::f64::consts::TAU * hz * f64::from(offset + i) / f64::from(rate)).sin()
                    as f32;
            capture.push([v, if opposite { -v } else { v }]);
        }
        capture.end(1);
        for _ in 0..100 {
            analysis.slice(|| {
                tick += 1;
                tick
            });
        }
        if let Some(frame) = analysis.take() {
            result = Some(frame);
        }
    }
    result.expect("completed analysis window")
}

#[test]
fn accuracy_passband_alias_rejection_and_stereo_phase_match_c_acceptance() {
    let peak = |f: &pumper_core::spectrum::Frame| *f.levels.iter().max().unwrap();
    assert_eq!(peak(&render_tone(48000, 1000.0, 0.0, false)), 0);
    let hz = 48000.0 * 500.0 / 2048.0;
    let full = render_tone(48000, hz, 1.0, false);
    let opposite = render_tone(48000, hz, 1.0, true);
    let quiet = render_tone(48000, hz, 0.5, false);
    assert!(peak(&full) >= 191);
    assert_eq!(full.levels, opposite.levels);
    assert!((179..=181).contains(&peak(&quiet)));
    let index = full
        .levels
        .iter()
        .enumerate()
        .max_by_key(|(_, level)| *level)
        .unwrap()
        .0;
    let expected = 255.0 * (hz / 20.0).ln() / 1000.0_f64.ln();
    assert!((index as f64 - expected).abs() < 2.0);
    for rate in RATES {
        let analysis = if rate.is_multiple_of(44100) {
            44100.0
        } else {
            48000.0
        };
        let hz = analysis * (20000.0_f64 * 2048.0 / analysis).floor() / 2048.0;
        assert!(
            peak(&render_tone(rate, hz, 1.0, false)) >= 189,
            "passband {rate}"
        );
        if rate > 48000 {
            assert!(
                peak(&render_tone(rate, analysis - 6000.0, 1.0, false)) <= 40,
                "alias {rate}"
            );
        }
    }
}

fn scheduled_frames(rate: u32, short: bool) -> u32 {
    let pipe = Box::new(Pipe::new());
    let (mut capture, consumer) = pipe.split().unwrap();
    let mut analysis = Analyzer::new(consumer);
    let mut tick = 0u32;
    analysis.start();
    capture.begin(rate, 1);
    capture.push([0.0; 2]);
    capture.end(1);
    for _ in 0..100 {
        analysis.slice(|| {
            tick += 1;
            tick
        });
    }
    let mut frames = 0;
    let limit = if short { rate * 60 / 1000 } else { rate / 2 };
    for offset in (0..limit).step_by(128) {
        capture.begin(rate, 1);
        for _ in 0..128 {
            capture.push([100.0, -100.0]);
        }
        capture.end(1);
        for _ in 0..if short { 100 } else { 8 } {
            analysis.slice(|| {
                tick += 1;
                tick
            });
        }
        if analysis.take().is_some() {
            frames += 1;
            let analysis_rate = if rate.is_multiple_of(44100) {
                44100
            } else {
                48000
            };
            assert!(offset + 128 >= 2048 * (rate / analysis_rate));
            if short {
                break;
            }
        }
    }
    frames
}

#[test]
fn complete_window_arrives_within_sixty_ms_at_every_rate() {
    for rate in RATES {
        assert!(scheduled_frames(rate, true) >= 1, "rate={rate}");
    }
}

#[test]
fn incremental_cadence_matches_c_synthetic_work_budget() {
    // Retain the C threshold and eight slices per capture block. Matching its
    // FFT batch sizes must pass even with Rust's extra post-decimator time
    // check. These synthetic clock ticks are not board microseconds.
    let frames = RATES.map(|rate| scheduled_frames(rate, false));
    eprintln!("synthetic cadence: rates={RATES:?}, frames={frames:?}");
    assert!(
        frames.iter().all(|n| *n >= 7),
        "rates={RATES:?}, frames={frames:?}; expected >=7 each"
    );
}
