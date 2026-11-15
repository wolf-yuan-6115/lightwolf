//! Owned configuration and byte-exact protocol/flash records.
use crate::protocol::{Status, milli};
pub const BANDS: usize = 10;
pub fn u16le(b: &[u8]) -> u16 {
    u16::from_le_bytes([b[0], b[1]])
}
pub fn u32le(b: &[u8]) -> u32 {
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
}
pub fn put16(b: &mut [u8], v: u16) {
    b[..2].copy_from_slice(&v.to_le_bytes());
}
pub fn put32(b: &mut [u8], v: u32) {
    b[..4].copy_from_slice(&v.to_le_bytes());
}
fn between(v: f32, min: f32, max: f32) -> bool {
    v.is_finite() && v >= min && v <= max
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Band {
    pub enabled: bool,
    pub kind: u8,
    pub width: u8,
    pub frequency: f32,
    pub q: f32,
    pub bw: f32,
    pub gain: f32,
}
impl Band {
    pub const fn new(kind: u8, width: u8, frequency: f32, q: f32, bw: f32) -> Self {
        Self {
            enabled: true,
            kind,
            width,
            frequency,
            q,
            bw,
            gain: 0.0,
        }
    }
    pub fn valid(&self) -> bool {
        self.kind <= 6
            && self.width <= 1
            && (self.kind < 3 || self.width == 0)
            && between(self.frequency, 20.0, 20000.0)
            && between(self.gain, -24.0, 24.0)
            && between(self.bw, 0.1, 4.0)
            && between(
                self.q,
                0.1,
                if self.kind == 0 || self.kind == 2 {
                    1.0
                } else {
                    20.0
                },
            )
    }
    pub fn encode(&self, index: u8) -> [u8; 21] {
        let mut b = [0; 21];
        b[..4].copy_from_slice(&[index, u8::from(self.enabled), self.kind, self.width]);
        for (pos, v) in [
            (5, self.frequency),
            (9, self.gain),
            (13, self.q),
            (17, self.bw),
        ] {
            put32(&mut b[pos..], milli(v) as u32);
        }
        b
    }
    pub fn decode(b: &[u8]) -> Result<(u8, Self), Status> {
        if b.len() != 21 {
            return Err(Status::InvalidLength);
        }
        if b[1] > 1 {
            return Err(Status::OutOfRange);
        }
        let value = |p: usize| (u32le(&b[p..]) as i32) as f32 / 1000.0;
        Ok((
            b[0],
            Self {
                enabled: b[1] != 0,
                kind: b[2],
                width: b[3],
                frequency: value(5),
                gain: value(9),
                q: value(13),
                bw: value(17),
            },
        ))
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Config {
    pub enabled: bool,
    pub preamp: f32,
    pub bands: [Band; 10],
}
impl Default for Config {
    fn default() -> Self {
        Self::FLAT
    }
}
impl Config {
    pub const FLAT: Self = Self {
        enabled: true,
        preamp: 0.0,
        bands: [
            Band::new(1, 1, 68.0, 0.71, 1.89),
            Band::new(0, 0, 105.0, 0.71, 1.89),
            Band::new(1, 1, 260.0, 4.0, 0.36),
            Band::new(1, 1, 1300.0, 3.0, 0.48),
            Band::new(1, 1, 1650.0, 3.0, 0.48),
            Band::new(1, 1, 2600.0, 5.0, 0.29),
            Band::new(2, 0, 3000.0, 0.35, 3.33),
            Band::new(1, 1, 3000.0, 1.4, 1.01),
            Band::new(1, 1, 5100.0, 4.5, 0.32),
            Band::new(2, 0, 10000.0, 0.71, 1.89),
        ],
    };
    pub fn valid(&self) -> bool {
        between(self.preamp, -241.0, 12.0) && self.bands.iter().all(Band::valid)
    }
    pub fn global(&self) -> [u8; 8] {
        let mut b = [0; 8];
        b[0] = u8::from(self.enabled);
        put32(&mut b[4..], milli(self.preamp) as u32);
        b
    }
    pub fn with_global(mut self, b: &[u8]) -> Result<Self, Status> {
        if b.len() != 8 {
            return Err(Status::InvalidLength);
        }
        if b[0] > 1 {
            return Err(Status::OutOfRange);
        }
        self.enabled = b[0] != 0;
        self.preamp = (u32le(&b[4..]) as i32) as f32 / 1000.0;
        if self.valid() {
            Ok(self)
        } else {
            Err(Status::OutOfRange)
        }
    }
    pub fn record(&self) -> [u8; 208] {
        let mut b = [0; 208];
        b[..8].copy_from_slice(&self.global());
        for (i, band) in self.bands.iter().enumerate() {
            b[8 + i * 20..28 + i * 20].copy_from_slice(&band.encode(i as u8)[1..]);
        }
        b
    }
    pub fn from_record(b: &[u8]) -> Option<Self> {
        if b.len() != 208 {
            return None;
        }
        let mut c = Self::FLAT.with_global(&b[..8]).ok()?;
        for i in 0..10 {
            let mut band = [0; 21];
            band[0] = i as u8;
            band[1..].copy_from_slice(&b[8 + i * 20..28 + i * 20]);
            c.bands[i] = Band::decode(&band).ok()?.1;
        }
        c.valid().then_some(c)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Crossfeed {
    pub mode: u8,
    pub strength: u16,
    pub cutoff: u16,
    pub delay: u16,
}
impl Default for Crossfeed {
    fn default() -> Self {
        Self::DEFAULT
    }
}
impl Crossfeed {
    pub const DEFAULT: Self = Self {
        mode: 0,
        strength: 2000,
        cutoff: 700,
        delay: 250,
    };
    pub fn valid(&self) -> bool {
        self.mode <= 4
            && self.strength <= 4000
            && (300..=2000).contains(&self.cutoff)
            && self.delay <= 600
    }
    pub fn encode(&self) -> [u8; 8] {
        let mut b = [0; 8];
        b[0] = self.mode;
        put16(&mut b[2..], self.strength);
        put16(&mut b[4..], self.cutoff);
        put16(&mut b[6..], self.delay);
        b
    }
    pub fn decode(b: &[u8]) -> Option<Self> {
        if b.len() != 8 || b[1] != 0 {
            return None;
        }
        let c = Self {
            mode: b[0],
            strength: u16le(&b[2..]),
            cutoff: u16le(&b[4..]),
            delay: u16le(&b[6..]),
        };
        c.valid().then_some(c)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Output {
    pub flags: u8,
    pub balance: i16,
    pub width: u16,
}
impl Default for Output {
    fn default() -> Self {
        Self::DEFAULT
    }
}
impl Output {
    pub const DEFAULT: Self = Self {
        flags: 0,
        balance: 0,
        width: 10000,
    };
    pub fn valid(&self) -> bool {
        self.flags & 0xe0 == 0 && (-10000..=10000).contains(&self.balance) && self.width <= 20000
    }
    pub fn encode(&self) -> [u8; 8] {
        let mut b = [0; 8];
        b[0] = self.flags;
        put16(&mut b[2..], self.balance as u16);
        put16(&mut b[4..], self.width);
        b
    }
    pub fn decode(b: &[u8]) -> Option<Self> {
        if b.len() != 8 || b[1] != 0 || b[6] != 0 || b[7] != 0 {
            return None;
        }
        let c = Self {
            flags: b[0],
            balance: u16le(&b[2..]) as i16,
            width: u16le(&b[4..]),
        };
        c.valid().then_some(c)
    }
}
#[derive(Clone, Copy, Debug)]
pub struct Runtime {
    pub eq: Config,
    pub crossfeed: Crossfeed,
    pub output: Output,
    pub gains: [f32; 2],
    pub generation: u32,
    pub controls_generation: u32,
}
impl Default for Runtime {
    fn default() -> Self {
        Self {
            eq: Config::FLAT,
            crossfeed: Crossfeed::DEFAULT,
            output: Output::DEFAULT,
            gains: [1.0; 2],
            generation: 1,
            controls_generation: 1,
        }
    }
}
