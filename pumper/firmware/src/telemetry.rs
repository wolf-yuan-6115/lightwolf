//! Sequence-checked atomic DSP snapshot; only core 1 writes its payload.
use core::sync::atomic::{AtomicU32, Ordering, fence};
#[derive(Clone, Copy, Default)]
pub struct AudioStats {
    pub generation: u32,
    pub frames: u64,
    pub us: u64,
    pub limited: u64,
    pub peak_us: u32,
    pub peak_frames: u32,
}
impl AudioStats {
    pub fn block(&mut self, generation: u32, frames: u32, us: u32, limited: u32) {
        if self.generation != generation {
            *self = Self {
                generation,
                ..Self::default()
            };
        }
        self.frames += u64::from(frames);
        self.us += u64::from(us);
        self.limited += u64::from(limited);
        if self.peak_frames == 0
            || u64::from(us) * u64::from(self.peak_frames)
                > u64::from(self.peak_us) * u64::from(frames)
        {
            self.peak_us = us;
            self.peak_frames = frames;
        }
    }
}
pub struct Snapshot {
    sequence: AtomicU32,
    words: [AtomicU32; 9],
}
impl Snapshot {
    pub const fn new() -> Self {
        Self {
            sequence: AtomicU32::new(0),
            words: [const { AtomicU32::new(0) }; 9],
        }
    }
    pub fn publish(&self, s: AudioStats) {
        self.sequence.fetch_add(1, Ordering::AcqRel);
        let w = [
            s.generation,
            s.frames as u32,
            (s.frames >> 32) as u32,
            s.us as u32,
            (s.us >> 32) as u32,
            s.limited as u32,
            (s.limited >> 32) as u32,
            s.peak_us,
            s.peak_frames,
        ];
        for (a, v) in self.words.iter().zip(w) {
            a.store(v, Ordering::Relaxed);
        }
        self.sequence.fetch_add(1, Ordering::Release);
    }
    pub fn read(&self) -> AudioStats {
        loop {
            let before = self.sequence.load(Ordering::Acquire);
            if before & 1 != 0 {
                continue;
            }
            let w = self.words.each_ref().map(|a| a.load(Ordering::Relaxed));
            fence(Ordering::Acquire);
            if before == self.sequence.load(Ordering::Relaxed) {
                return AudioStats {
                    generation: w[0],
                    frames: u64::from(w[1]) | (u64::from(w[2]) << 32),
                    us: u64::from(w[3]) | (u64::from(w[4]) << 32),
                    limited: u64::from(w[5]) | (u64::from(w[6]) << 32),
                    peak_us: w[7],
                    peak_frames: w[8],
                };
            }
        }
    }
}
pub static AUDIO: Snapshot = Snapshot::new();
