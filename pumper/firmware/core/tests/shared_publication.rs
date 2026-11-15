//! Exercise the actual device snapshot implementations on host threads. Payloads
//! must remain internally consistent while the sole writer publishes repeatedly.
#[allow(dead_code, unexpected_cfgs)]
#[path = "../../src/shared.rs"]
mod shared;
#[allow(dead_code)]
#[path = "../../src/telemetry.rs"]
mod telemetry;
use pumper_core::{format::Format, usb::Stream};
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};

#[test]
fn atomic_stream_and_64_bit_telemetry_payloads_never_tear() {
    let stream = Arc::new(shared::Shared::new());
    let audio = Arc::new(telemetry::Snapshot::new());
    let done = Arc::new(AtomicBool::new(false));
    let (writer_stream, writer_audio, writer_done) = (stream.clone(), audio.clone(), done.clone());
    let writer = std::thread::spawn(move || {
        for generation in 1..=100_000_u32 {
            let even = generation.is_multiple_of(2);
            writer_stream.publish(Stream {
                rate: if even { 96000 } else { 48000 },
                format: if even { Format::Pcm24 } else { Format::Pcm16 },
                alternate: if even { 2 } else { 1 },
                generation,
            });
            writer_audio.publish(telemetry::AudioStats {
                generation,
                frames: (u64::from(generation) << 32) | u64::from(!generation),
                us: u64::from(generation) * 123456789,
                limited: u64::from(generation) * 987654321,
                peak_us: generation ^ 0xa5a5a5a5,
                peak_frames: generation ^ 0x5a5a5a5a,
            });
            if generation.is_multiple_of(17) {
                std::thread::yield_now();
            }
        }
        writer_done.store(true, Ordering::Release);
    });
    let mut reads = 0;
    loop {
        let s = stream.stream();
        if s.generation != 0 {
            let even = s.generation.is_multiple_of(2);
            assert_eq!(s.rate, if even { 96000 } else { 48000 });
            assert_eq!(s.format, if even { Format::Pcm24 } else { Format::Pcm16 });
            assert_eq!(s.alternate, if even { 2 } else { 1 });
        }
        let a = audio.read();
        let g = a.generation;
        if g != 0 {
            assert_eq!(a.frames, (u64::from(g) << 32) | u64::from(!g));
            assert_eq!(a.us, u64::from(g) * 123456789);
            assert_eq!(a.limited, u64::from(g) * 987654321);
            assert_eq!(a.peak_us, g ^ 0xa5a5a5a5);
            assert_eq!(a.peak_frames, g ^ 0x5a5a5a5a);
        }
        reads += 1;
        if done.load(Ordering::Acquire) && reads >= 1000 {
            break;
        }
    }
    writer.join().unwrap();
}
