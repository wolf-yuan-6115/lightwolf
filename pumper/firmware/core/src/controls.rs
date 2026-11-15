use crate::config::{Crossfeed, Output};
use core::f32::consts::PI;
#[derive(Clone, Copy)]
struct CrossProcessor {
    strength: f32,
    alpha: f32,
    fraction: f32,
    normalizer: f32,
    delay: usize,
    position: usize,
    low: [f32; 2],
    history: [[f32; 128]; 2],
}
impl CrossProcessor {
    fn new(rate: u32, c: Crossfeed) -> Self {
        let mut s = Self {
            strength: 0.0,
            alpha: 0.0,
            fraction: 0.0,
            normalizer: 1.0,
            delay: 0,
            position: 0,
            low: [0.0; 2],
            history: [[0.0; 128]; 2],
        };
        s.configure(rate, c);
        s
    }
    fn configure(&mut self, rate: u32, c: Crossfeed) {
        self.strength = if c.mode == 4 {
            c.strength
        } else {
            [0, 1000, 2000, 3000][c.mode as usize]
        } as f32
            / 10000.0;
        self.normalizer = 1.0 / (1.0 + self.strength);
        let cutoff = if c.mode == 4 { c.cutoff } else { 700 };
        let us = if c.mode == 4 {
            c.delay
        } else {
            [0, 200, 250, 300][c.mode as usize]
        };
        self.alpha = 1.0 - libm::expf(-2.0 * PI * cutoff as f32 / rate as f32);
        let delay = us as f32 * rate as f32 / 1000000.0;
        self.delay = delay as usize;
        self.fraction = delay - self.delay as f32;
    }
    #[inline(always)]
    fn process(&mut self, input: [f32; 2]) -> [f32; 2] {
        let mut delayed = [0.0; 2];
        for i in 0..2 {
            self.low[i] += self.alpha * (input[i] - self.low[i]);
            self.history[i][self.position] = self.low[i];
            let a = (self.position + 128 - self.delay) & 127;
            let b = (a + 127) & 127;
            delayed[i] =
                self.history[i][a] + self.fraction * (self.history[i][b] - self.history[i][a]);
        }
        self.position = (self.position + 1) & 127;
        if self.strength == 0.0 {
            input
        } else {
            [
                (input[0] + self.strength * delayed[1]) * self.normalizer,
                (input[1] + self.strength * delayed[0]) * self.normalizer,
            ]
        }
    }
}
fn matrix(c: Output) -> [f32; 4] {
    let mut m = if c.flags & 2 != 0 {
        [0.0, 1.0, 1.0, 0.0]
    } else {
        [1.0, 0.0, 0.0, 1.0]
    };
    if c.flags & 4 != 0 {
        m[0] = -m[0];
        m[1] = -m[1];
    }
    if c.flags & 8 != 0 {
        m[2] = -m[2];
        m[3] = -m[3];
    }
    let width = if c.flags & 1 != 0 {
        0.0
    } else {
        c.width as f32 / 10000.0
    };
    let same = 0.5 * (1.0 + width);
    let opposite = 0.5 * (1.0 - width);
    let mut out = [
        same * m[0] + opposite * m[2],
        same * m[1] + opposite * m[3],
        opposite * m[0] + same * m[2],
        opposite * m[1] + same * m[3],
    ];
    if c.balance > 0 {
        let s = 1.0 - c.balance as f32 / 10000.0;
        out[0] *= s;
        out[1] *= s;
    } else if c.balance < 0 {
        let s = 1.0 + c.balance as f32 / 10000.0;
        out[2] *= s;
        out[3] *= s;
    }
    out
}
pub struct Controls {
    cross: Crossfeed,
    output: Output,
    current: CrossProcessor,
    next: CrossProcessor,
    pending: Option<Crossfeed>,
    rate: u32,
    ramp: u32,
    fade: u32,
    gain: [f32; 2],
    target_gain: [f32; 2],
    gain_step: [f32; 2],
    gain_remaining: u32,
    matrix: [f32; 4],
    target_matrix: [f32; 4],
    matrix_step: [f32; 4],
    matrix_remaining: u32,
    limiter_gain: f32,
    release: f32,
}
impl Controls {
    pub fn new(rate: u32, cross: Crossfeed, output: Output) -> Self {
        let current = CrossProcessor::new(rate, cross);
        let m = matrix(output);
        Self {
            cross,
            output,
            current,
            next: current,
            pending: None,
            rate,
            ramp: (rate / 100).max(1),
            fade: 0,
            gain: [1.0; 2],
            target_gain: [1.0; 2],
            gain_step: [0.0; 2],
            gain_remaining: 0,
            matrix: m,
            target_matrix: m,
            matrix_step: [0.0; 4],
            matrix_remaining: 0,
            limiter_gain: 1.0,
            release: libm::expf(-1.0 / (rate as f32 * 0.050)),
        }
    }
    pub fn reset(&mut self, rate: u32) {
        let gains = self.target_gain;
        *self = Self::new(rate, self.cross, self.output);
        self.gain = gains;
        self.target_gain = gains;
    }
    pub fn configure(&mut self, c: Crossfeed, o: Output, g: [f32; 2]) {
        if c != self.cross && c.valid() {
            self.cross = c;
            if self.fade != 0 {
                self.pending = Some(c);
            } else {
                self.next = self.current;
                self.next.configure(self.rate, c);
                self.fade = self.ramp;
            }
        }
        if o != self.output && o.valid() {
            self.output = o;
            self.target_matrix = matrix(o);
            let scale = 1.0 / self.ramp as f32;
            for i in 0..4 {
                self.matrix_step[i] = (self.target_matrix[i] - self.matrix[i]) * scale;
            }
            self.matrix_remaining = self.ramp;
        }
        if g != self.target_gain {
            self.target_gain = g;
            for (i, value) in g.iter().enumerate() {
                self.gain_step[i] = (*value - self.gain[i]) / self.ramp as f32;
            }
            self.gain_remaining = self.ramp;
        }
    }
    #[inline(always)]
    pub fn process(&mut self, input: [f32; 2]) -> ([f32; 2], bool) {
        let mut p = input;
        if self.current.strength != 0.0 || self.fade != 0 {
            p = self.current.process(input);
        }
        if self.fade != 0 {
            let next = self.next.process(input);
            let blend = (self.ramp - self.fade + 1) as f32 / self.ramp as f32;
            for i in 0..2 {
                p[i] += blend * (next[i] - p[i]);
            }
            self.fade -= 1;
            if self.fade == 0 {
                self.current = self.next;
                if let Some(c) = self.pending.take() {
                    self.next = self.current;
                    self.next.configure(self.rate, c);
                    self.fade = self.ramp;
                }
            }
        }
        if self.gain_remaining != 0 {
            self.gain_remaining -= 1;
            for i in 0..2 {
                self.gain[i] = self.target_gain[i] - self.gain_step[i] * self.gain_remaining as f32;
            }
        }
        for (i, v) in p.iter_mut().enumerate() {
            *v *= self.gain[i];
        }
        if self.matrix_remaining != 0 {
            self.matrix_remaining -= 1;
            for i in 0..4 {
                self.matrix[i] =
                    self.target_matrix[i] - self.matrix_step[i] * self.matrix_remaining as f32;
            }
            if self.matrix_remaining == 0 {
                self.matrix = self.target_matrix;
            }
        }
        let out = [
            self.matrix[0] * p[0] + self.matrix[1] * p[1],
            self.matrix[2] * p[0] + self.matrix[3] * p[1],
        ];
        let peak = out[0].abs().max(out[1].abs());
        let target = if !peak.is_finite() {
            0.0
        } else if peak <= 32768.0 {
            1.0
        } else {
            32768.0 / peak
        };
        let previous_gain = self.limiter_gain;
        self.limiter_gain = if target < previous_gain {
            target
        } else {
            target + (self.limiter_gain - target) * self.release
        };
        // At high rates, f32 release steps can round back to the previous
        // gain before reaching the fixed unity threshold. Finish that stalled
        // release instead of retaining a tiny attenuation (and LIM) forever.
        // A steady overload has target < 1 and must never take this path.
        if target == 1.0 && (self.limiter_gain > 0.9999 || self.limiter_gain == previous_gain) {
            self.limiter_gain = 1.0;
        }
        let gain = self.limiter_gain;
        (
            out.map(|v| {
                let v = v * gain;
                if v.is_finite() {
                    v.clamp(-32768.0, 32767.0)
                } else {
                    0.0
                }
            }),
            gain < 1.0,
        )
    }
}
