//! Diagnostic-only stack sentinels. Each core inspects only its own unused stack
//! with local interrupts masked; core 0 never races reads of core 1's stack.
use core::sync::atomic::{AtomicU32, Ordering};
pub static CORE0_FREE: AtomicU32 = AtomicU32::new(131072);
pub static CORE1_FREE: AtomicU32 = AtomicU32::new(32768);
static CORE1_BASE: AtomicU32 = AtomicU32::new(0);
const PATTERN: u32 = 0xa55aa55a;
pub fn prepare_core1(stack: &mut embassy_rp::multicore::Stack<32768>) {
    let base = stack as *mut _ as *mut u32;
    for i in 0..32768 / 4 {
        unsafe {
            base.add(i).write_volatile(PATTERN);
        }
    }
    CORE1_BASE.store(base as u32, Ordering::Release);
}
pub struct StackScan {
    base: usize,
    end: usize,
    cursor: usize,
    core1: bool,
}
impl StackScan {
    pub fn new(core1: bool) -> Self {
        let (base, end) = if core1 {
            let b = CORE1_BASE.load(Ordering::Acquire) as usize;
            (b + 32, b + 32768)
        } else {
            (0x20082000 - 131072, 0x20082000)
        };
        Self {
            base,
            end,
            cursor: base,
            core1,
        }
    }
    pub fn step(&mut self) {
        let free = if self.core1 { &CORE1_FREE } else { &CORE0_FREE };
        cortex_m::interrupt::free(|_| {
            let sp = cortex_m::register::msp::read() as usize;
            if sp <= self.base {
                free.store(0, Ordering::Relaxed);
                return;
            }
            free.fetch_min((sp.min(self.end) - self.base) as u32, Ordering::Relaxed);
            let ceiling = sp.saturating_sub(256).min(self.end);
            for _ in 0..16 {
                if self.cursor >= ceiling {
                    self.cursor = self.base;
                    break;
                }
                // Owned stack reservation, below current frames and with no interrupt writer.
                let value = unsafe { (self.cursor as *const u32).read_volatile() };
                if value != PATTERN {
                    free.fetch_min((self.cursor - self.base) as u32, Ordering::Relaxed);
                    self.cursor = self.base;
                    break;
                }
                self.cursor += 4;
            }
        });
    }
}
