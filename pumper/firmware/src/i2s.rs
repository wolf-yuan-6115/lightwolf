//! Core-1-only PIO/DMA owner. No ISR or other core accesses these channels.
//! Completion is polled in the dedicated audio loop; the hardware chains channels.
use crate::{
    audio::{Block, Sender},
    shared::SHARED,
};
use core::sync::atomic::{Ordering, fence};
use embassy_rp::pac;
use pumper_core::format::Format;

static SILENCE: [u32; 388] = [0; 388];

pub struct I2s {
    active: [Option<&'static mut Block>; 2],
    pending: heapless::Deque<&'static mut Block, 6>,
    rate: u32,
    format: Format,
    streaming: bool,
    priming: bool,
    recovering: bool,
    buffered: u32,
    phase: u32,
}
impl I2s {
    pub fn new() -> Self {
        // Ownership of PIO0, DMA0/1 and GPIO19..21 is moved into core1 in main.
        pac::RESETS.reset().modify(|w| {
            w.set_pio0(false);
            w.set_dma(false);
        });
        while !pac::RESETS.reset_done().read().pio0() || !pac::RESETS.reset_done().read().dma() {}
        for pin in 19..=21 {
            pac::IO_BANK0.gpio(pin).ctrl().write(|w| w.set_funcsel(6));
            pac::PADS_BANK0.gpio(pin).modify(|w| {
                w.set_ie(true);
                w.set_od(false);
                w.set_iso(false);
            });
        }
        let program = pio::pio_asm!(
            ".side_set 2",
            "right:",
            "out pins, 1 side 2",
            "jmp x-- right side 3",
            "out pins, 1 side 0",
            "set x, 14 side 1",
            "left:",
            "out pins, 1 side 0",
            "jmp x-- left side 1",
            "out pins, 1 side 2",
            "set x, 14 side 3"
        );
        for (i, instruction) in program.program.code.iter().enumerate() {
            pac::PIO0
                .instr_mem(i)
                .write(|w| w.0 = u32::from(*instruction));
            // Relocate the two JMPs and use 30 for the 32-bit slot loop.
            let mut value = *instruction;
            if value & 0xe000 == 0 {
                value = (value & !31) | ((value + 8) & 31);
            }
            if i == 3 || i == 7 {
                value = (value & !31) | 30;
            }
            pac::PIO0.instr_mem(i + 8).write(|w| w.0 = u32::from(value));
        }
        Self {
            active: [None, None],
            pending: heapless::Deque::new(),
            rate: 48000,
            format: Format::Pcm16,
            streaming: false,
            priming: true,
            recovering: false,
            buffered: 0,
            phase: 0,
        }
    }
    pub fn configure(&mut self, rate: u32, format: Format, streaming: bool, free: &mut Sender) {
        // Stop chaining before aborting, then wait before returning DMA-owned memory.
        for i in 0..2 {
            pac::DMA.ch(i).ctrl_trig().modify(|w| w.set_en(false));
        }
        pac::DMA.chan_abort().write(|w| w.0 = 3);
        let started = embassy_time::Instant::now().as_micros();
        while pac::DMA.chan_abort().read().0 & 3 != 0 {
            assert!(
                embassy_time::Instant::now().as_micros() - started < 1000,
                "DMA abort timeout"
            );
        }
        fence(Ordering::SeqCst);
        pac::DMA.intr(0).write_value(3);
        for slot in &mut self.active {
            if let Some(block) = slot.take() {
                crate::shared::owned(free.enqueue(block));
            }
        }
        while let Some(block) = self.pending.pop_front() {
            crate::shared::owned(free.enqueue(block));
        }
        self.rate = rate;
        self.format = format;
        self.streaming = streaming;
        self.priming = true;
        self.recovering = false;
        self.buffered = 0;
        self.phase = 0;
        SHARED.buffered.store(0, Ordering::Release);
        SHARED.peak.store(0, Ordering::Relaxed);
        SHARED.block_us.store(0, Ordering::Relaxed);
        SHARED.dsp_us.store(0, Ordering::Relaxed);
        SHARED.high_water.store(0, Ordering::Relaxed);
        SHARED.low_water.store(u32::MAX, Ordering::Relaxed);
        pac::PIO0.ctrl().write(|w| w.0 = 0);
        crate::hardware::set_clock(streaming, rate, format);
        let sm = pac::PIO0.sm(0);
        pac::PIO0.ctrl().write(|w| w.0 = 0);
        // Toggle FIFO join to clear both FIFOs, restart shift/clock state.
        sm.shiftctrl().write(|w| w.0 = 0);
        sm.shiftctrl().write(|w| w.0 = (1 << 30) | (1 << 17));
        pac::PIO0.ctrl().write(|w| w.0 = (1 << 4) | (1 << 8));
        let offset = if format == Format::Pcm16 { 0 } else { 8 };
        sm.execctrl()
            .write(|w| w.0 = ((offset + 7) << 12) | (offset << 7));
        sm.pinctrl()
            .write(|w| w.0 = 21 | (19 << 5) | (19 << 10) | (1 << 20) | (3 << 26) | (2 << 29));
        sm.instr().write(|w| w.0 = 0xe087); // SET PINDIRS, 7
        sm.instr().write(|w| w.0 = 0xe000); // SET PINS, 0
        sm.instr().write(|w| w.0 = offset + 7);
        let factor = if format == Format::Pcm16 { 4 } else { 2 };
        let divider = (crate::hardware::clock_hz() * factor + rate / 2) / rate;
        sm.clkdiv().write(|w| w.0 = divider << 8);
        for i in 0..2 {
            pac::DMA
                .ch(i)
                .write_addr()
                .write_value(pac::PIO0.txf(0).as_ptr() as u32);
            // EN, WORD transfers, increment source, chain to partner, PIO0_TX0 DREQ.
            pac::DMA
                .ch(i)
                .al1_ctrl()
                .write(|w| *w = 1 | (2 << 2) | (1 << 4) | (((i ^ 1) as u32) << 13));
            self.prepare(i);
        }
        pac::PIO0.ctrl().write(|w| w.0 = 1);
        pac::DMA.multi_chan_trigger().write(|w| w.0 = 1);
    }
    pub fn flash_mute(&mut self, free: &mut Sender) {
        self.configure(self.rate, self.format, false, free);
        for i in 0..2 {
            pac::DMA.ch(i).ctrl_trig().modify(|w| w.set_en(false));
        }
        pac::DMA.chan_abort().write(|w| w.0 = 3);
        let started = embassy_time::Instant::now().as_micros();
        while pac::DMA.chan_abort().read().0 & 3 != 0 {
            assert!(
                embassy_time::Instant::now().as_micros() - started < 1000,
                "flash mute DMA abort timeout"
            );
        }
        fence(Ordering::SeqCst);
        pac::DMA.intr(0).write_value(3);
        // Endless, single-word RAM silence continues while the flash worker runs.
        pac::DMA
            .ch(0)
            .read_addr()
            .write_value(SILENCE.as_ptr() as u32);
        pac::DMA.ch(0).trans_count().write(|w| w.0 = 0xffff_ffff);
        pac::DMA.ch(0).ctrl_trig().write(|w| w.0 = 1 | (2 << 2));
    }
    pub fn submit(&mut self, block: &'static mut Block) {
        self.buffered += block.frames as u32;
        SHARED
            .high_water
            .fetch_max(self.buffered, Ordering::Relaxed);
        crate::shared::owned(self.pending.push_back(block));
        SHARED.buffered.store(self.buffered, Ordering::Release);
    }
    fn prepare(&mut self, index: usize) {
        if self.priming && self.buffered >= (self.rate * 2).div_ceil(1000) {
            self.priming = false;
            self.recovering = false;
        }
        let next = if self.streaming && !self.priming {
            self.pending.pop_front()
        } else {
            None
        };
        let (source, words) = if let Some(block) = next {
            let pair = (block.words.as_ptr(), block.word_count);
            self.active[index] = Some(block);
            pair
        } else {
            self.phase += self.rate % 1000;
            let frames = self.rate / 1000 + self.phase / 1000;
            self.phase %= 1000;
            if self.streaming && (!self.priming || self.recovering) {
                SHARED.underruns.fetch_add(frames, Ordering::Relaxed);
                self.recovering = true;
            }
            self.priming = true;
            (
                SILENCE.as_ptr(),
                frames as usize * if self.format == Format::Pcm16 { 1 } else { 2 },
            )
        };
        fence(Ordering::Release);
        pac::DMA.ch(index).read_addr().write_value(source as u32);
        pac::DMA
            .ch(index)
            .trans_count()
            .write(|w| w.0 = words as u32);
    }
    pub fn service(&mut self, free: &mut Sender) {
        let done = pac::DMA.intr(0).read() & 3;
        for index in 0..2 {
            if done & (1 << index) == 0 {
                continue;
            }
            pac::DMA.intr(0).write_value(1 << index);
            fence(Ordering::Acquire);
            if let Some(block) = self.active[index].take() {
                self.buffered -= block.frames as u32;
                crate::shared::owned(free.enqueue(block));
                SHARED.buffered.store(self.buffered, Ordering::Release);
                SHARED.low_water.fetch_min(self.buffered, Ordering::Relaxed);
            }
            self.prepare(index);
        }
    }
}
