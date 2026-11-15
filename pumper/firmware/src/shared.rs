use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use pumper_core::usb::Stream;

pub struct Shared {
    sequence: AtomicU32,
    pub rate: AtomicU32,
    pub alternate: AtomicU32,
    format: AtomicU32,
    pub generation: AtomicU32,
    pub flash_busy: AtomicBool,
    pub meter_epoch: AtomicU32,
    pub meter_enabled: AtomicBool,
    pub eq_applied: AtomicU32,
    pub high_water: AtomicU32,
    pub feedback: AtomicU32,
    pub buffered: AtomicU32,
    pub underruns: AtomicU32,
    pub packets: AtomicU32,
    pub resets: AtomicU32,
    pub backpressure: AtomicU32,
    pub block_us: AtomicU32,
    pub dsp_us: AtomicU32,
    pub temperature: AtomicU32,
    pub spectrum_us: AtomicU32,
    pub spectrum_frames: AtomicU32,
    pub spectrum_drops: AtomicU32,
    pub peak: AtomicU32,
    pub low_water: AtomicU32,
    pub applied: AtomicU32,
}
impl Shared {
    pub const fn new() -> Self {
        Self {
            sequence: AtomicU32::new(0),
            format: AtomicU32::new(16),
            rate: AtomicU32::new(48000),
            alternate: AtomicU32::new(0),
            generation: AtomicU32::new(0),
            flash_busy: AtomicBool::new(false),
            meter_epoch: AtomicU32::new(0),
            meter_enabled: AtomicBool::new(false),
            eq_applied: AtomicU32::new(0),
            high_water: AtomicU32::new(0),
            feedback: AtomicU32::new(0),
            buffered: AtomicU32::new(0),
            underruns: AtomicU32::new(0),
            packets: AtomicU32::new(0),
            resets: AtomicU32::new(0),
            backpressure: AtomicU32::new(0),
            block_us: AtomicU32::new(0),
            dsp_us: AtomicU32::new(0),
            temperature: AtomicU32::new(0),
            spectrum_us: AtomicU32::new(0),
            spectrum_frames: AtomicU32::new(0),
            spectrum_drops: AtomicU32::new(0),
            peak: AtomicU32::new(0),
            low_water: AtomicU32::new(u32::MAX),
            applied: AtomicU32::new(u32::MAX),
        }
    }
    // Core 0 is the sole publisher; atomic payloads make retry reads race-free.
    pub fn publish(&self, stream: Stream) {
        self.sequence.fetch_add(1, Ordering::AcqRel);
        self.rate.store(stream.rate, Ordering::Relaxed);
        self.format
            .store(u32::from(stream.format.bits()), Ordering::Relaxed);
        self.alternate
            .store(u32::from(stream.alternate), Ordering::Relaxed);
        self.generation.store(stream.generation, Ordering::Relaxed);
        self.sequence.fetch_add(1, Ordering::Release);
    }
    pub fn stream(&self) -> Stream {
        loop {
            let seq = self.sequence.load(Ordering::Acquire);
            if seq & 1 != 0 {
                continue;
            }
            let rate = self.rate.load(Ordering::Relaxed);
            let bits = self.format.load(Ordering::Relaxed);
            let alternate = self.alternate.load(Ordering::Relaxed) as u8;
            let generation = self.generation.load(Ordering::Relaxed);
            core::sync::atomic::fence(Ordering::Acquire);
            if seq == self.sequence.load(Ordering::Relaxed) {
                return Stream {
                    rate,
                    alternate,
                    generation,
                    format: if bits == 24 {
                        pumper_core::format::Format::Pcm24
                    } else {
                        pumper_core::format::Format::Pcm16
                    },
                };
            }
        }
    }
}
pub static SHARED: Shared = Shared::new();

#[cfg(feature = "diagnostics")]
pub static OWNERSHIP_FAULTS: AtomicU32 = AtomicU32::new(0);
/// A capacity failure is a pool conservation fault: fail before reusing a token.
pub fn owned<T, E>(result: Result<T, E>) -> T {
    match result {
        Ok(value) => value,
        Err(_) => {
            #[cfg(feature = "diagnostics")]
            OWNERSHIP_FAULTS.fetch_add(1, Ordering::Relaxed);
            panic!("audio pool ownership fault");
        }
    }
}
