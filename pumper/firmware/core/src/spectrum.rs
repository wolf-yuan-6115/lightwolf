//! Producer/consumer capture with atomic payloads and block-end metadata acknowledgement.
use crate::spectrum_tables::*;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering, fence};
const N: usize = 2048;
pub const FIFO: usize = 16384;
#[derive(Clone, Copy, Default)]
struct Pair {
    r: f32,
    i: f32,
}
#[derive(Clone, Copy, Debug)]
pub struct Frame {
    pub sequence: u32,
    pub rate: u32,
    pub levels: [u8; 256],
}
impl Frame {
    pub fn chunk(&self, index: u8) -> Option<([u8; 56], usize)> {
        if index >= 6 {
            return None;
        }
        let count = if index == 5 { 36 } else { 44 };
        let mut b = [0; 56];
        b[..4].copy_from_slice(&[1, index, 6, count as u8]);
        b[4..8].copy_from_slice(&self.sequence.to_le_bytes());
        b[8..12].copy_from_slice(&self.rate.to_le_bytes());
        b[12..12 + count]
            .copy_from_slice(&self.levels[index as usize * 44..index as usize * 44 + count]);
        Some((b, 12 + count))
    }
}
pub struct Pipe {
    samples: [[AtomicU32; 2]; FIFO],
    write: AtomicU32,
    read: AtomicU32,
    epoch: AtomicU32,
    enabled: AtomicBool,
    taken: AtomicBool,
    meta_sequence: AtomicU32,
    producer_epoch: AtomicU32,
    generation: AtomicU32,
    rate: AtomicU32,
    pub dropped: AtomicU32,
    pub high_water: AtomicU32,
    pub capture_us: AtomicU32,
}
impl Default for Pipe {
    fn default() -> Self {
        Self::new()
    }
}
impl Pipe {
    pub const fn new() -> Self {
        Self {
            samples: [const { [AtomicU32::new(0), AtomicU32::new(0)] }; FIFO],
            write: AtomicU32::new(0),
            read: AtomicU32::new(0),
            epoch: AtomicU32::new(0),
            enabled: AtomicBool::new(false),
            taken: AtomicBool::new(false),
            meta_sequence: AtomicU32::new(0),
            producer_epoch: AtomicU32::new(0),
            generation: AtomicU32::new(0),
            rate: AtomicU32::new(0),
            dropped: AtomicU32::new(0),
            high_water: AtomicU32::new(0),
            capture_us: AtomicU32::new(0),
        }
    }
    pub fn split(&self) -> Option<(Producer<'_>, Consumer<'_>)> {
        if self.taken.swap(true, Ordering::AcqRel) {
            return None;
        }
        Some((
            Producer {
                pipe: self,
                cursor: 0,
                epoch: 0,
                generation: 0,
                rate: 0,
                high: 0,
                active: false,
            },
            Consumer { pipe: self },
        ))
    }
}
pub struct Producer<'a> {
    pipe: &'a Pipe,
    cursor: u32,
    epoch: u32,
    generation: u32,
    rate: u32,
    high: u32,
    active: bool,
}
impl Producer<'_> {
    pub fn begin(&mut self, rate: u32, generation: u32) -> bool {
        self.active = self.pipe.enabled.load(Ordering::Acquire);
        if self.active {
            self.cursor = self.pipe.write.load(Ordering::Relaxed);
            self.epoch = self.pipe.epoch.load(Ordering::Acquire);
            self.generation = generation;
            self.rate = rate;
            self.high = 0;
        }
        self.active
    }
    #[inline]
    pub fn push(&mut self, p: [f32; 2]) {
        if !self.active {
            return;
        }
        let depth = self
            .cursor
            .wrapping_sub(self.pipe.read.load(Ordering::Acquire));
        if depth >= FIFO as u32 {
            self.pipe.dropped.fetch_add(1, Ordering::Relaxed);
            return;
        }
        let slot = &self.pipe.samples[self.cursor as usize & (FIFO - 1)];
        slot[0].store(p[0].to_bits(), Ordering::Relaxed);
        slot[1].store(p[1].to_bits(), Ordering::Relaxed);
        self.cursor = self.cursor.wrapping_add(1);
        self.high = self.high.max(depth + 1);
    }
    pub fn end(&mut self, elapsed: u32) {
        if !self.active {
            return;
        }
        self.pipe.high_water.fetch_max(self.high, Ordering::Relaxed);
        self.pipe.capture_us.fetch_max(elapsed, Ordering::Relaxed);
        self.pipe.meta_sequence.fetch_add(1, Ordering::AcqRel);
        self.pipe.write.store(self.cursor, Ordering::Release);
        self.pipe
            .producer_epoch
            .store(self.epoch, Ordering::Relaxed);
        self.pipe
            .generation
            .store(self.generation, Ordering::Relaxed);
        self.pipe.rate.store(self.rate, Ordering::Relaxed);
        self.pipe.meta_sequence.fetch_add(1, Ordering::Release);
        self.active = false;
    }
}
pub struct Consumer<'a> {
    pipe: &'a Pipe,
}
impl Consumer<'_> {
    fn invalidate(&mut self) {
        self.pipe.epoch.fetch_add(1, Ordering::AcqRel);
        self.flush();
    }
    fn flush(&mut self) {
        self.pipe
            .read
            .store(self.pipe.write.load(Ordering::Acquire), Ordering::Release);
    }
    fn metadata(&self) -> Option<[u32; 4]> {
        let seq = self.pipe.meta_sequence.load(Ordering::Acquire);
        if seq & 1 != 0 {
            return None;
        }
        let values = [
            self.pipe.producer_epoch.load(Ordering::Relaxed),
            self.pipe.generation.load(Ordering::Relaxed),
            self.pipe.rate.load(Ordering::Relaxed),
            self.pipe.dropped.load(Ordering::Relaxed),
        ];
        fence(Ordering::Acquire);
        if seq != self.pipe.meta_sequence.load(Ordering::Relaxed)
            || values[0] != self.pipe.epoch.load(Ordering::Acquire)
        {
            None
        } else {
            Some(values)
        }
    }
    fn pop(&mut self) -> Option<Pair> {
        let read = self.pipe.read.load(Ordering::Relaxed);
        if read == self.pipe.write.load(Ordering::Acquire) {
            return None;
        }
        let slot = &self.pipe.samples[read as usize & (FIFO - 1)];
        let p = Pair {
            r: f32::from_bits(slot[0].load(Ordering::Relaxed)),
            i: f32::from_bits(slot[1].load(Ordering::Relaxed)),
        };
        self.pipe
            .read
            .store(read.wrapping_add(1), Ordering::Release);
        Some(p)
    }
    fn empty(&self) -> bool {
        self.pipe.read.load(Ordering::Relaxed) == self.pipe.write.load(Ordering::Acquire)
    }
}
#[derive(Clone, Copy)]
struct Decimator {
    history: [Pair; 256],
    index: usize,
    phase: bool,
}
impl Decimator {
    const EMPTY: Self = Self {
        history: [Pair { r: 0.0, i: 0.0 }; 256],
        index: 0,
        phase: false,
    };
    fn push(&mut self, p: Pair) -> Option<Pair> {
        self.history[self.index] = p;
        self.history[self.index + 128] = p;
        self.index = (self.index + 1) & 127;
        self.phase = !self.phase;
        if self.phase {
            return None;
        }
        let mut sum = Pair::default();
        let newest = self.index + 127;
        let oldest = self.index + 1;
        for tap in (0..63).step_by(2) {
            let c = SPECTRUM_FIR[tap];
            sum.r += c * (self.history[newest - tap].r + self.history[oldest + tap].r);
            sum.i += c * (self.history[newest - tap].i + self.history[oldest + tap].i);
        }
        let center = self.history[self.index + 64];
        sum.r += SPECTRUM_FIR[63] * center.r;
        sum.i += SPECTRUM_FIR[63] * center.i;
        Some(sum)
    }
}
pub struct Analyzer<'a> {
    consumer: Consumer<'a>,
    history: [Pair; N],
    fft: [Pair; N],
    power: [f32; N / 2 + 1],
    decimator: [Decimator; 2],
    seen: [u32; 4],
    rate: u32,
    factor: u32,
    index: usize,
    samples: usize,
    hop: u32,
    stage: u8,
    position: usize,
    length: usize,
    base: usize,
    butterfly: usize,
    sequence: u32,
    build: Frame,
    result: Option<Frame>,
    pub completed: u32,
    pub dropped: u32,
    pub max_slice_us: u32,
}
impl<'a> Analyzer<'a> {
    pub fn new(consumer: Consumer<'a>) -> Self {
        Self {
            consumer,
            history: [Pair::default(); N],
            fft: [Pair::default(); N],
            power: [0.0; N / 2 + 1],
            decimator: [Decimator::EMPTY; 2],
            seen: [u32::MAX; 4],
            rate: 0,
            factor: 1,
            index: 0,
            samples: 0,
            hop: 0,
            stage: 0,
            position: 0,
            length: 0,
            base: 0,
            butterfly: 0,
            sequence: 0,
            build: Frame {
                sequence: 0,
                rate: 0,
                levels: [0; 256],
            },
            result: None,
            completed: 0,
            dropped: 0,
            max_slice_us: 0,
        }
    }
    fn clear(&mut self) {
        self.stage = 0;
        self.position = 0;
        self.index = 0;
        self.samples = 0;
        self.hop = 0;
        self.decimator = [Decimator::EMPTY; 2];
        self.result = None;
        self.consumer.flush();
    }
    pub fn reset(&mut self) {
        self.consumer.invalidate();
        self.clear();
    }
    pub fn start(&mut self) {
        self.reset();
        self.sequence = 0;
        self.consumer.pipe.enabled.store(true, Ordering::Release);
    }
    pub fn stop(&mut self) {
        self.consumer.pipe.enabled.store(false, Ordering::Release);
        self.reset();
    }
    fn sync(&mut self) -> bool {
        let Some(meta) = self.consumer.metadata() else {
            return false;
        };
        if meta != self.seen {
            self.seen = meta;
            self.factor = if meta[2] > 96000 {
                4
            } else if meta[2] > 48000 {
                2
            } else {
                1
            };
            self.rate = meta[2] / self.factor;
            self.clear();
        }
        self.rate == 44100 || self.rate == 48000
    }
    fn gather(&mut self) {
        let Some(mut p) = self.consumer.pop() else {
            return;
        };
        if self.factor >= 2 {
            let Some(v) = self.decimator[0].push(p) else {
                return;
            };
            p = v;
        }
        if self.factor == 4 {
            let Some(v) = self.decimator[1].push(p) else {
                return;
            };
            p = v;
        }
        self.history[self.index] = p;
        self.index = (self.index + 1) & (N - 1);
        if self.samples < N {
            self.samples += 1;
            if self.samples < N {
                return;
            }
        } else {
            self.hop += 1;
            if self.hop < self.rate / 20 {
                return;
            }
        }
        self.hop = 0;
        if self.stage != 0 {
            self.dropped = self.dropped.wrapping_add(1);
            return;
        }
        self.stage = 1;
        self.position = 0;
    }
    fn analyze(&mut self) {
        match self.stage {
            1 => {
                let p = self.position;
                self.position += 1;
                let v = self.history[(self.index + p) & (N - 1)];
                let w = SPECTRUM_WINDOW[p] / 32768.0;
                let reversed = (p as u32).reverse_bits() >> (32 - 11);
                self.fft[reversed as usize] = Pair {
                    r: v.r * w,
                    i: v.i * w,
                };
                if self.position == N {
                    self.stage = 2;
                    self.length = 2;
                    self.base = 0;
                    self.butterfly = 0;
                }
            }
            2 => {
                let a = self.base + self.butterfly;
                let b = a + self.length / 2;
                let t = self.butterfly * (N / self.length);
                let x = self.fft[a];
                let y = self.fft[b];
                let z = Pair {
                    r: y.r * SPECTRUM_COS[t] - y.i * SPECTRUM_SIN[t],
                    i: y.r * SPECTRUM_SIN[t] + y.i * SPECTRUM_COS[t],
                };
                self.fft[a] = Pair {
                    r: x.r + z.r,
                    i: x.i + z.i,
                };
                self.fft[b] = Pair {
                    r: x.r - z.r,
                    i: x.i - z.i,
                };
                self.butterfly += 1;
                if self.butterfly == self.length / 2 {
                    self.butterfly = 0;
                    self.base += self.length;
                    if self.base == N {
                        self.base = 0;
                        self.length *= 2;
                        if self.length > N {
                            self.stage = 3;
                            self.position = 0;
                        }
                    }
                }
            }
            3 => {
                let k = self.position;
                self.position += 1;
                let a = self.fft[k];
                let b = self.fft[(N - k) & (N - 1)];
                let lr = (a.r + b.r) * 0.5;
                let li = (a.i - b.i) * 0.5;
                let rr = (a.i + b.i) * 0.5;
                let ri = (b.r - a.r) * 0.5;
                let scale = if k == 0 || k == N / 2 {
                    2.0 / N as f32
                } else {
                    4.0 / N as f32
                };
                self.power[k] = 0.5 * (lr * lr + li * li + rr * rr + ri * ri) * scale * scale;
                if self.position == N / 2 + 1 {
                    self.stage = 4;
                    self.position = 0;
                }
            }
            4 => {
                let p = self.position;
                self.position += 1;
                let rate = self.rate as f32;
                let bin = SPECTRUM_FREQUENCY[p] * N as f32 / rate;
                let low = if p == 0 {
                    20.0
                } else {
                    libm::sqrtf(SPECTRUM_FREQUENCY[p - 1] * SPECTRUM_FREQUENCY[p])
                } * N as f32
                    / rate;
                let high = if p == 255 {
                    20000.0
                } else {
                    libm::sqrtf(SPECTRUM_FREQUENCY[p] * SPECTRUM_FREQUENCY[p + 1])
                } * N as f32
                    / rate;
                let power = if high - low < 1.0 {
                    let k = bin as usize;
                    self.power[k] + (self.power[k + 1] - self.power[k]) * (bin - k as f32)
                } else {
                    let mut power = 0.0f32;
                    for k in libm::ceilf(low) as usize..=libm::floorf(high) as usize {
                        power = power.max(self.power[k]);
                    }
                    power
                };
                let code = if power > 0.0 {
                    (10.0 * libm::log10f(power) + 96.0) * 2.0
                } else {
                    0.0
                };
                self.build.levels[p] = if code <= 0.0 {
                    0
                } else if code >= 192.0 {
                    192
                } else {
                    (code + 0.5) as u8
                };
                if self.position == 256 {
                    self.sequence = self.sequence.wrapping_add(1);
                    self.build.sequence = self.sequence;
                    self.build.rate = self.rate;
                    self.result = Some(self.build);
                    self.completed = self.completed.wrapping_add(1);
                    self.stage = 0;
                }
            }
            _ => {}
        }
    }
    fn analyze_batch(&mut self) {
        let stage = self.stage;
        let batch = match stage {
            2 => 8,
            1 | 3 => 4,
            _ => 1,
        };
        for _ in 0..batch {
            if self.stage == 0 {
                break;
            }
            self.analyze();
            // Never carry a cheap-stage batch into expensive point mapping.
            // Mapping must remain one point between timer checks.
            if self.stage != stage {
                break;
            }
        }
    }
    pub fn slice(&mut self, mut clock: impl FnMut() -> u32) {
        let start = clock();
        if !self.consumer.pipe.enabled.load(Ordering::Acquire) || !self.sync() {
            return;
        }
        loop {
            if self.stage != 1 {
                self.gather();
                if clock().wrapping_sub(start) >= 40 {
                    break;
                }
            }
            self.analyze_batch();
            if (self.stage == 0 && self.consumer.empty()) || clock().wrapping_sub(start) >= 40 {
                break;
            }
        }
        self.sync();
        self.max_slice_us = self.max_slice_us.max(clock().wrapping_sub(start));
    }
    pub fn take(&mut self) -> Option<Frame> {
        if self.sync() {
            self.result.take()
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    #[test]
    fn batches_stop_before_entering_point_mapping() {
        let pipe = std::boxed::Box::new(Pipe::new());
        let (_, consumer) = pipe.split().unwrap();
        let mut analyzer = Analyzer::new(consumer);
        analyzer.rate = 48000;
        analyzer.stage = 3;
        analyzer.position = N / 2;
        analyzer.analyze_batch();
        assert_eq!((analyzer.stage, analyzer.position), (4, 0));
        analyzer.analyze_batch();
        assert_eq!((analyzer.stage, analyzer.position), (4, 1));
    }
}
