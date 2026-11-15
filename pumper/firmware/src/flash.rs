//! Flash worker on core 1. All executable code/constants are RAM-resident;
//! core 0 continues USB service and never accesses XIP while a job is in flight.
use core::sync::atomic::{Ordering, fence};
use embassy_rp::{pac, rom_data};
use pumper_core::storage::{self, Error, Flash};
#[cfg(feature = "diagnostics")]
pub static LAST_RESULT: core::sync::atomic::AtomicI32 = core::sync::atomic::AtomicI32::new(0);
pub type Queue = heapless::spsc::Queue<&'static mut Job, 2>;
pub type Sender = heapless::spsc::Producer<'static, &'static mut Job>;
pub type Receiver = heapless::spsc::Consumer<'static, &'static mut Job>;
#[repr(C, align(16))]
pub struct Job {
    pub image: [u8; 4096],
    pub bank: u8,
    pub reset: bool,
    pub result: Result<(), Error>,
}
impl Job {
    pub const fn new() -> Self {
        Self {
            image: [0xff; 4096],
            bank: 0,
            reset: false,
            result: Ok(()),
        }
    }
}
pub struct SettingsFlash;
impl SettingsFlash {
    fn bounds(offset: usize, len: usize) -> Result<(), Error> {
        if offset.checked_add(len).is_none_or(|end| end > 8192) {
            Err(Error::Invalid)
        } else {
            Ok(())
        }
    }
    fn operation(op: u32, offset: usize, len: usize, ptr: *mut u8) -> Result<(), Error> {
        Self::bounds(offset, len)?;
        if pac::SIO.cpuid().read() != 1 {
            return Err(Error::Invalid);
        }
        // BOOTLOCK1 protects the ROM flash-operation workspace. Never spin for it.
        if pac::BOOTRAM.bootlock(1).read() == 0 {
            #[cfg(feature = "diagnostics")]
            LAST_RESULT.store(-100, Ordering::Relaxed);
            return Err(Error::Io);
        }
        fence(Ordering::Acquire);
        // SAFETY: only this worker writes flash. Its caller has stopped DSP and
        // configured RAM-backed endless DMA silence. Core 0 and all ISRs execute from
        // RAM, and core 0 cannot read settings while a job owns the flash mailbox.
        let result = cortex_m::interrupt::free(|_| unsafe {
            rom_data::flash_op(
                0x100 | (op << 16),
                0x101fe000 + offset as u32,
                len as u32,
                ptr,
            )
        });
        // Direct XIP verification must not observe lines cached before programming.
        unsafe {
            rom_data::flash_flush_cache();
        }
        #[cfg(feature = "diagnostics")]
        LAST_RESULT.store(result, Ordering::Relaxed);
        fence(Ordering::Release);
        pac::BOOTRAM.bootlock(1).write_value(0);
        if result == 0 { Ok(()) } else { Err(Error::Io) }
    }
}
impl Flash for SettingsFlash {
    fn read(&mut self, offset: usize, out: &mut [u8]) -> Result<(), Error> {
        Self::bounds(offset, out.len())?;
        // Boot reads happen before core 1 starts; later reads occur only after the
        // completion token returns (or inside core 1 between ROM operations).
        if pac::SIO.cpuid().read() == 0 && crate::shared::SHARED.flash_busy.load(Ordering::Acquire)
        {
            return Err(Error::Io);
        }
        unsafe {
            core::ptr::copy_nonoverlapping(
                (0x101fe000 + offset) as *const u8,
                out.as_mut_ptr(),
                out.len(),
            );
        }
        Ok(())
    }
    fn erase(&mut self, offset: usize, len: usize) -> Result<(), Error> {
        if !offset.is_multiple_of(4096) || !len.is_multiple_of(4096) {
            return Err(Error::Invalid);
        }
        Self::operation(0, offset, len, core::ptr::null_mut())
    }
    fn program(&mut self, offset: usize, data: &[u8]) -> Result<(), Error> {
        if !offset.is_multiple_of(256) || !data.len().is_multiple_of(256) {
            return Err(Error::Invalid);
        }
        Self::operation(1, offset, data.len(), data.as_ptr().cast_mut())
    }
}
pub fn execute(job: &mut Job) {
    let mut flash = SettingsFlash;
    job.result = if job.reset {
        storage::factory_reset(&mut flash)
    } else {
        storage::commit(&mut flash, job.bank, &job.image)
    };
    #[cfg(feature = "diagnostics")]
    if LAST_RESULT.load(Ordering::Relaxed) == 0 {
        LAST_RESULT.store(
            match job.result {
                Ok(()) => 0,
                Err(Error::Corrupt) => -101,
                Err(Error::Invalid) => -102,
                Err(Error::Io) => -103,
            },
            Ordering::Relaxed,
        );
    }
}
