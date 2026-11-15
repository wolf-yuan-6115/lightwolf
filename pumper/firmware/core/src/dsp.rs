//! Allocation-free stereo EQ and processing chain. The observer runs inside the sample loop.
use crate::{
    config::{Band, Config, Runtime},
    controls::Controls,
};
use core::f32::consts::PI;
const ID: [f32; 5] = [1.0, 0.0, 0.0, 0.0, 0.0];
fn coefficients(c: Band, fs: f32) -> Option<[f32; 5]> {
    if !c.enabled || (c.kind <= 2 && c.gain.abs() < 0.0001) || c.frequency >= fs * 0.5 {
        return Some(ID);
    }
    let w0 = 2.0 * PI * c.frequency / fs;
    let sin = libm::sinf(w0);
    let cos = libm::cosf(w0);
    let a = libm::powf(10.0, c.gain / 40.0);
    let (b0, b1, b2, a0, a1, a2);
    if c.kind == 1 {
        let alpha = if c.width == 1 {
            if sin.abs() < 1e-12 {
                return None;
            }
            sin * libm::sinhf((libm::logf(2.0) * 0.5) * c.bw * (w0 / sin))
        } else {
            sin / (2.0 * c.q)
        };
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cos;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cos;
        a2 = 1.0 - alpha / a;
    } else if c.kind == 0 || c.kind == 2 {
        let alpha = sin * 0.5 * libm::sqrtf((a + (1.0 / a)) * ((1.0 / c.q) - 1.0) + 2.0);
        let t = 2.0 * libm::sqrtf(a) * alpha;
        if c.kind == 0 {
            b0 = a * ((a + 1.0) - (a - 1.0) * cos + t);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cos);
            b2 = a * ((a + 1.0) - (a - 1.0) * cos - t);
            a0 = (a + 1.0) + (a - 1.0) * cos + t;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cos);
            a2 = (a + 1.0) + (a - 1.0) * cos - t;
        } else {
            b0 = a * ((a + 1.0) + (a - 1.0) * cos + t);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cos);
            b2 = a * ((a + 1.0) + (a - 1.0) * cos - t);
            a0 = (a + 1.0) - (a - 1.0) * cos + t;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cos);
            a2 = (a + 1.0) - (a - 1.0) * cos - t;
        }
    } else {
        let alpha = sin / (2.0 * c.q);
        a0 = 1.0 + alpha;
        a1 = -2.0 * cos;
        a2 = 1.0 - alpha;
        (b0, b1, b2) = match c.kind {
            3 => ((1.0 - cos) * 0.5, 1.0 - cos, (1.0 - cos) * 0.5),
            4 => ((1.0 + cos) * 0.5, -(1.0 + cos), (1.0 + cos) * 0.5),
            5 => (1.0, -2.0 * cos, 1.0),
            _ => (alpha, 0.0, -alpha),
        };
    }
    if a0.abs() < 1e-12 {
        None
    } else {
        Some([b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0])
    }
}
#[derive(Clone, Copy)]
struct Biquad {
    current: [f32; 5],
    target: [f32; 5],
    delta: [f32; 5],
    z: [[f32; 2]; 2],
}
impl Biquad {
    const EMPTY: Self = Self {
        current: ID,
        target: ID,
        delta: [0.0; 5],
        z: [[0.0; 2]; 2],
    };
    #[inline(always)]
    fn process(&mut self, p: [f32; 2]) -> [f32; 2] {
        let c = self.current;
        let left = c[0] * p[0] + self.z[0][0];
        let right = c[0] * p[1] + self.z[1][0];
        self.z[0][0] = c[1] * p[0] - c[3] * left + self.z[0][1];
        self.z[1][0] = c[1] * p[1] - c[3] * right + self.z[1][1];
        self.z[0][1] = c[2] * p[0] - c[4] * left;
        self.z[1][1] = c[2] * p[1] - c[4] * right;
        [left, right]
    }
}
#[derive(Clone, Copy, Default, Debug)]
pub struct Metrics {
    pub peaks: [u16; 4],
    pub squares: [u64; 4],
    pub frames: u32,
    pub limiter_frames: u32,
}
impl Metrics {
    #[inline]
    fn measure(&mut self, p: [f32; 2], offset: usize, rms: bool) {
        for (i, s) in p.into_iter().enumerate() {
            let m = if s.abs() >= 32768.0 {
                32768
            } else {
                (s.abs() + 0.5) as u32
            };
            self.peaks[offset + i] = self.peaks[offset + i].max(m as u16);
            if rms {
                self.squares[offset + i] += u64::from(m) * u64::from(m);
            }
        }
    }
    pub fn merge(&mut self, other: Self) {
        for i in 0..4 {
            self.peaks[i] = self.peaks[i].max(other.peaks[i]);
            self.squares[i] = self.squares[i].saturating_add(other.squares[i]);
        }
        self.frames = self.frames.saturating_add(other.frames);
        self.limiter_frames = self.limiter_frames.saturating_add(other.limiter_frames);
    }
    pub fn encode(&self, sequence: u32) -> [u8; 32] {
        use crate::config::{put16, put32};
        let mut b = [0; 32];
        put32(&mut b, sequence);
        for (i, peak) in self.peaks.iter().enumerate() {
            let p = if i < 2 { 4 + i * 2 } else { 16 + (i - 2) * 2 };
            put16(&mut b[p..], *peak);
            let p = if i < 2 { 8 + i * 4 } else { 20 + (i - 2) * 4 };
            put32(
                &mut b[p..],
                if self.frames == 0 {
                    0
                } else {
                    (self.squares[i] / u64::from(self.frames)) as u32
                },
            );
        }
        b[28] = u8::from(self.limiter_frames != 0);
        b
    }
}
pub struct Dsp {
    bands: [Biquad; 10],
    active: [usize; 10],
    count: usize,
    transitioning: [usize; 10],
    transition_count: usize,
    config: Config,
    rate: u32,
    ramp: u32,
    remaining: u32,
    preamp: f32,
    target_preamp: f32,
    delta_preamp: f32,
    pub controls: Controls,
}
impl Dsp {
    pub fn new(rate: u32, config: Runtime) -> Self {
        let mut s = Self {
            bands: [Biquad::EMPTY; 10],
            active: [0; 10],
            count: 0,
            transitioning: [0; 10],
            transition_count: 0,
            config: config.eq,
            rate,
            ramp: (rate / 100).max(1),
            remaining: 0,
            preamp: 1.0,
            target_preamp: 1.0,
            delta_preamp: 0.0,
            controls: Controls::new(rate, config.crossfeed, config.output),
        };
        s.targets(config.eq, true);
        s.controls
            .configure(config.crossfeed, config.output, config.gains);
        s
    }
    fn rebuild_active(&mut self, transition: bool) {
        self.count = 0;
        for (i, b) in self.bands.iter().enumerate() {
            if b.target != ID || (transition && b.current != ID) {
                self.active[self.count] = i;
                self.count += 1;
            }
        }
    }
    fn targets(&mut self, c: Config, immediate: bool) -> bool {
        if !c.valid() {
            return false;
        }
        let mut next = [ID; 10];
        if c.enabled {
            for (i, b) in c.bands.iter().enumerate() {
                let Some(v) = coefficients(*b, self.rate as f32) else {
                    return false;
                };
                next[i] = v;
            }
        }
        self.config = c;
        self.target_preamp = if c.enabled {
            libm::powf(10.0, c.preamp / 20.0)
        } else {
            1.0
        };
        self.transition_count = 0;
        for (i, b) in self.bands.iter_mut().enumerate() {
            b.target = next[i];
            if immediate {
                b.current = next[i];
                b.delta = [0.0; 5];
            } else {
                let scale = 1.0 / self.ramp as f32;
                for (j, value) in next[i].iter().enumerate() {
                    b.delta[j] = (*value - b.current[j]) * scale;
                }
                if b.delta.iter().any(|v| *v != 0.0) {
                    self.transitioning[self.transition_count] = i;
                    self.transition_count += 1;
                }
            }
        }
        if immediate {
            self.preamp = self.target_preamp;
            self.delta_preamp = 0.0;
            self.remaining = 0;
            self.rebuild_active(false);
        } else {
            self.delta_preamp = (self.target_preamp - self.preamp) / self.ramp as f32;
            self.remaining = self.ramp;
            self.rebuild_active(true);
        }
        true
    }
    pub fn set_config(&mut self, c: Config) -> bool {
        self.targets(c, false)
    }
    pub fn set_rate(&mut self, rate: u32) {
        if rate != 0 && self.rate != rate {
            self.rate = rate;
            self.ramp = (rate / 100).max(1);
            self.targets(self.config, true);
            self.reset_state();
            self.controls.reset(rate);
        }
    }
    pub fn reset_state(&mut self) {
        for b in &mut self.bands {
            b.z = [[0.0; 2]; 2];
        }
    }
    pub fn reset_stream(&mut self) {
        self.reset_state();
        self.controls.reset(self.rate);
    }
    fn advance(&mut self) {
        if self.remaining == 0 {
            return;
        }
        self.remaining -= 1;
        let r = self.remaining as f32;
        self.preamp = self.target_preamp - self.delta_preamp * r;
        for &index in &self.transitioning[..self.transition_count] {
            let b = &mut self.bands[index];
            for j in 0..5 {
                b.current[j] = b.target[j] - b.delta[j] * r;
            }
        }
        if self.remaining == 0 {
            self.preamp = self.target_preamp;
            for b in &mut self.bands {
                b.current = b.target;
            }
            self.rebuild_active(false);
        }
    }
    #[inline(never)]
    pub fn process(
        &mut self,
        samples: &mut [f32],
        rms: bool,
        mut observe: impl FnMut([f32; 2]),
    ) -> Metrics {
        let mut metrics = Metrics::default();
        for pair in samples.chunks_exact_mut(2) {
            self.advance();
            let input = [pair[0], pair[1]];
            metrics.measure(input, 0, rms);
            let mut p = input.map(|x| x * self.preamp);
            for &i in &self.active[..self.count] {
                p = self.bands[i].process(p);
            }
            let (p, limited) = self.controls.process(p);
            observe(p);
            pair.copy_from_slice(&p);
            metrics.measure(p, 2, rms);
            metrics.frames += 1;
            metrics.limiter_frames += u32::from(limited);
        }
        metrics
    }
}
