//! PCM decoding and final I2S conversion. Samples use the C engine's 32768 scale.
pub const RATES: [u32; 6] = [44_100, 48_000, 88_200, 96_000, 176_400, 192_000];
pub const MAX_FRAMES: usize = 194;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Format {
    Pcm16,
    Pcm24,
}

impl Format {
    pub const fn frame_bytes(self) -> usize {
        match self {
            Self::Pcm16 => 4,
            Self::Pcm24 => 6,
        }
    }
    pub const fn bits(self) -> u8 {
        match self {
            Self::Pcm16 => 16,
            Self::Pcm24 => 24,
        }
    }
    pub fn accepts(self, rate: u32) -> bool {
        RATES.contains(&rate) && (self == Self::Pcm16 || rate <= 96_000)
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum Error {
    InvalidLength,
    Capacity,
}

pub fn decode(output: &mut [f32], input: &[u8], format: Format) -> Result<usize, Error> {
    let bytes = format.frame_bytes();
    if !input.len().is_multiple_of(bytes) {
        return Err(Error::InvalidLength);
    }
    let frames = input.len() / bytes;
    if frames > output.len() / 2 {
        return Err(Error::Capacity);
    }
    for (frame, samples) in input.chunks_exact(bytes).zip(output.chunks_exact_mut(2)) {
        for (channel, sample) in samples.iter_mut().enumerate() {
            *sample = match format {
                Format::Pcm16 => f32::from(i16::from_le_bytes([
                    frame[channel * 2],
                    frame[channel * 2 + 1],
                ])),
                Format::Pcm24 => {
                    let p = &frame[channel * 3..];
                    let value = i32::from_le_bytes([0, p[0], p[1], p[2]]) >> 8;
                    value as f32 / 256.0
                }
            };
        }
    }
    Ok(frames)
}

fn quantize(sample: f32, scale: f32, min: i32, max: i32) -> i32 {
    let scaled = sample * scale;
    if scaled >= max as f32 {
        return max;
    }
    if scaled <= min as f32 {
        return min;
    }
    (if scaled < 0.0 {
        scaled - 0.5
    } else {
        scaled + 0.5
    }) as i32
}

/// Separate input/output borrows: the device's block storage changes representation
/// only after this function completes, never through overlapping Rust references.
pub fn pack(output: &mut [u32], input: &[f32], format: Format) -> Result<usize, Error> {
    if !input.len().is_multiple_of(2) {
        return Err(Error::InvalidLength);
    }
    let frames = input.len() / 2;
    let words = if format == Format::Pcm16 {
        frames
    } else {
        frames * 2
    };
    if output.len() < words {
        return Err(Error::Capacity);
    }
    for (frame, pair) in input.chunks_exact(2).enumerate() {
        match format {
            Format::Pcm16 => {
                let l = quantize(pair[0], 0.75, -24576, 24575);
                let r = quantize(pair[1], 0.75, -24576, 24575);
                output[frame] = u32::from(r as u16) | (u32::from(l as u16) << 16);
            }
            Format::Pcm24 => {
                let l = quantize(pair[0], 192.0, -6291456, 6291455);
                let r = quantize(pair[1], 192.0, -6291456, 6291455);
                output[frame * 2] = (l as u32 & 0xffffff) << 8;
                output[frame * 2 + 1] = (r as u32 & 0xffffff) << 8;
            }
        }
    }
    Ok(words)
}
