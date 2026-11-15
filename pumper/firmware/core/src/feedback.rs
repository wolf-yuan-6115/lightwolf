#[derive(Clone, Copy, Debug)]
pub struct Feedback {
    pub nominal: u32,
    target: i32,
    filtered: i32,
}
impl Feedback {
    pub fn new(rate: u32) -> Self {
        let target = ((rate * 2).div_ceil(1000) + rate.div_ceil(1000)) as i32;
        Self {
            nominal: ((u64::from(rate) << 16) / 1000) as u32,
            target,
            filtered: target << 3,
        }
    }
    pub fn update(&mut self, buffered_frames: u32) -> u32 {
        self.filtered += ((buffered_frames as i32 * 8) - self.filtered) >> 3;
        let correction = ((self.target - (self.filtered >> 3)) * 256).clamp(-32768, 32768);
        (self.nominal as i32 + correction) as u32
    }
    pub fn packet(value: u32, macos: bool) -> ([u8; 4], usize) {
        if macos {
            ((value >> 2).to_le_bytes(), 3)
        } else {
            (value.to_le_bytes(), 4)
        }
    }
}
