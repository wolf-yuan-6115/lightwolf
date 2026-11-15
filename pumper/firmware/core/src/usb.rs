use crate::format::{Format, RATES};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub enum Host {
    #[default]
    Unknown,
    Linux,
    Windows,
    Macos,
}

#[derive(Default)]
pub struct HostDetection {
    first: u8,
    second: u8,
}
impl HostDetection {
    pub fn observe(&mut self, descriptor: u8) {
        if descriptor == 1 {
            *self = Self::default();
            return;
        }
        if ![2, 3, 15].contains(&descriptor) {
            return;
        }
        if self.first == 0 {
            self.first = descriptor;
        } else if self.second == 0 && self.first != descriptor {
            self.second = descriptor;
        }
    }
    pub fn host(&self) -> Host {
        match (self.first, self.second) {
            (15, 2) => Host::Linux,
            (2, 15) => Host::Windows,
            (3, 15 | 2) => Host::Macos,
            _ => Host::Unknown,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Stream {
    pub rate: u32,
    pub format: Format,
    pub alternate: u8,
    pub generation: u32,
}
impl Default for Stream {
    fn default() -> Self {
        Self {
            rate: 48000,
            format: Format::Pcm16,
            alternate: 0,
            generation: 0,
        }
    }
}
impl Stream {
    pub fn set_rate(&mut self, rate: u32) -> bool {
        if !RATES.contains(&rate) || (self.alternate != 0 && !self.format.accepts(rate)) {
            return false;
        }
        self.rate = rate;
        self.generation = self.generation.wrapping_add(1);
        true
    }
    pub fn accepts_alt(&self, alt: u8) -> bool {
        match alt {
            0 => true,
            1 => Format::Pcm16.accepts(self.rate),
            2 => Format::Pcm24.accepts(self.rate),
            _ => false,
        }
    }
    pub fn set_alt(&mut self, alt: u8) -> bool {
        if !self.accepts_alt(alt) {
            return false;
        }
        self.alternate = alt;
        if alt != 0 {
            self.format = if alt == 1 {
                Format::Pcm16
            } else {
                Format::Pcm24
            };
        }
        self.generation = self.generation.wrapping_add(1);
        true
    }
}

pub const HID_REPORT_BYTES: &[u8] = crate::descriptors::DESC_HID_REPORT;

// USB Audio volume values are signed dB in units of 1/256 dB.
pub const VOLUME_MIN: i16 = -100 * 256;
pub const VOLUME_MAX: i16 = 0;
pub const VOLUME_STEP: i16 = 128;

pub fn valid_volume(value: i16) -> bool {
    (VOLUME_MIN..=VOLUME_MAX).contains(&value) && value % VOLUME_STEP == 0
}

pub fn volume_range() -> [u8; 8] {
    let mut range = [0; 8];
    range[..2].copy_from_slice(&1u16.to_le_bytes());
    range[2..4].copy_from_slice(&VOLUME_MIN.to_le_bytes());
    range[4..6].copy_from_slice(&VOLUME_MAX.to_le_bytes());
    range[6..8].copy_from_slice(&VOLUME_STEP.to_le_bytes());
    range
}

/// UAC entities belong to AudioControl interface zero and expose master-channel
/// controls only. Do not let a matching high byte route a request for another
/// interface (or a device/endpoint recipient) into audio state changes.
pub fn audio_control_address(
    interface_recipient: bool,
    index: u16,
    value: u16,
) -> Option<(u8, u8)> {
    if !interface_recipient || index as u8 != 0 || value as u8 != 0 {
        return None;
    }
    let entity = (index >> 8) as u8;
    matches!(entity, 2 | 4).then_some((entity, (value >> 8) as u8))
}

/// Preserve the selected alternate across USB suspend. Reset/unconfigure cancels
/// the saved alternate so a later resume cannot restart a disconnected stream.
#[derive(Default)]
pub struct Suspension {
    alternate: Option<u8>,
}
impl Suspension {
    pub fn update(&mut self, stream: &mut Stream, suspended: bool) {
        if suspended {
            if self.alternate.is_none() {
                self.alternate = Some(stream.alternate);
                stream.set_alt(0);
            }
        } else if let Some(alternate) = self.alternate.take() {
            stream.set_alt(alternate);
        }
    }
    pub fn cancel(&mut self) {
        self.alternate = None;
    }
}
