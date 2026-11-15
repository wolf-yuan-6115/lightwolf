#[derive(Default)]
pub struct Activity {
    phase: u8,
    started: u32,
    pending: bool,
}
impl Activity {
    pub fn notify(&mut self, now: u32) {
        if self.phase == 0 {
            self.phase = 1;
            self.started = now;
        } else {
            self.pending = true;
        }
    }
    pub fn level(&mut self, now: u32, streaming: bool) -> bool {
        if !streaming {
            *self = Self::default();
            return false;
        }
        if self.phase != 0 && now.wrapping_sub(self.started) >= 25_000 {
            if self.phase == 1 {
                self.phase = 2;
                self.started = now;
            } else if self.pending {
                self.phase = 1;
                self.pending = false;
                self.started = now;
            } else {
                self.phase = 0;
            }
        }
        self.phase != 1
    }
}
