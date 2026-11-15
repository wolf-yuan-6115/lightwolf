use crate::{audio::MeterBlock, shared::SHARED, usb::State};
use core::{cell::RefCell, sync::atomic::Ordering};
use pumper_core::{
    config::{put16, put32},
    controller::{DeviceStatus, Effect, Reply},
    dsp::Metrics,
    protocol::{self, Packet, Status},
    spectrum::{Analyzer, Frame},
    storage::{Action, Store},
};
struct PendingFlash {
    op: u8,
    id: u16,
    action: Action,
    candidate: Option<Store>,
}
pub struct Service {
    configs: heapless::spsc::Producer<'static, pumper_core::config::Runtime>,
    meters: heapless::spsc::Consumer<'static, MeterBlock>,
    flash_tx: crate::flash::Sender,
    flash_rx: crate::flash::Receiver,
    job: Option<&'static mut crate::flash::Job>,
    pending: Option<PendingFlash>,
    pub spectrum: Analyzer<'static>,
    spectrum_enabled: bool,
    sending: Option<Frame>,
    waiting: Option<Frame>,
    chunk: u8,
    meter: Metrics,
    meter_epoch: u32,
    boot: u64,
    stream_generation: u32,
    streaming: bool,
    started: u64,
    duration: u32,
    starts: u32,
    flash_failures: u32,
    meter_reports: u32,
}
impl Service {
    pub fn new(
        configs: heapless::spsc::Producer<'static, pumper_core::config::Runtime>,
        meters: heapless::spsc::Consumer<'static, MeterBlock>,
        flash_tx: crate::flash::Sender,
        flash_rx: crate::flash::Receiver,
        job: &'static mut crate::flash::Job,
        consumer: pumper_core::spectrum::Consumer<'static>,
        now: u64,
    ) -> Self {
        Self {
            configs,
            meters,
            flash_tx,
            flash_rx,
            job: Some(job),
            pending: None,
            spectrum: Analyzer::new(consumer),
            spectrum_enabled: false,
            sending: None,
            waiting: None,
            chunk: 0,
            meter: Metrics::default(),
            meter_epoch: 0,
            boot: now,
            stream_generation: 0,
            streaming: false,
            started: 0,
            duration: 0,
            starts: 0,
            flash_failures: 0,
            meter_reports: 0,
        }
    }
    fn clear_spectrum(&mut self) {
        self.spectrum.reset();
        self.sending = None;
        self.waiting = None;
        self.chunk = 0;
    }
    fn status(&self) -> DeviceStatus {
        let s = SHARED.stream();
        let low = SHARED.low_water.load(Ordering::Relaxed);
        DeviceStatus {
            streaming: s.alternate != 0 && !SHARED.flash_busy.load(Ordering::Acquire),
            rate: s.rate,
            bits: s.format.bits(),
            applied: SHARED.eq_applied.load(Ordering::Acquire),
            underruns: SHARED.underruns.load(Ordering::Relaxed),
            backpressure: SHARED.backpressure.load(Ordering::Relaxed),
            temperature: SHARED.temperature.load(Ordering::Relaxed) as i32,
            clock: crate::hardware::clock_hz(),
            dsp_us: SHARED.dsp_us.load(Ordering::Relaxed),
            low_water: if low == u32::MAX { 0 } else { low },
        }
    }
    fn telemetry(&self, s: &State, now: u64) -> [u8; 56] {
        let stream = SHARED.stream();
        let active = stream.alternate != 0 && !SHARED.flash_busy.load(Ordering::Acquire);
        let mut a = crate::telemetry::AUDIO.read();
        if a.generation != stream.generation {
            a = Default::default();
        }
        let mut p = [0; 56];
        p[0] = 1;
        p[1] = if active { 3 } else { 0 };
        let saturate = |v: u64| v.min(u64::from(u32::MAX)) as u32;
        let nominal = ((u64::from(stream.rate) << 16) / 1000) as u32;
        let feedback = SHARED.feedback.load(Ordering::Relaxed);
        let ppm = if active && nominal != 0 {
            ((i64::from(feedback) - i64::from(nominal)) * 1_000_000 / i64::from(nominal)) as i32
        } else {
            0
        };
        let duration = if self.streaming {
            (now - self.started) / 1000
        } else {
            u64::from(self.duration)
        };
        for (offset, value) in [
            (4, saturate((now - self.boot) / 1_000_000)),
            (8, saturate(duration)),
            (12, SHARED.buffered.load(Ordering::Relaxed)),
            (16, SHARED.high_water.load(Ordering::Relaxed)),
            (20, ppm as u32),
            (
                28,
                if stream.rate == 0 {
                    0
                } else {
                    saturate(a.limited * 1000 / u64::from(stream.rate))
                },
            ),
            (32, SHARED.packets.load(Ordering::Relaxed)),
            (36, self.starts),
            (40, s.malformed),
            (44, s.busy),
            (48, self.flash_failures),
            (52, self.meter_reports),
        ] {
            put32(&mut p[offset..], value);
        }
        let load = |us: u64, frames: u64| {
            if frames == 0 {
                0
            } else {
                (us * u64::from(stream.rate / 100) / frames).min(u64::from(u16::MAX)) as u16
            }
        };
        put16(&mut p[24..], load(a.us, a.frames));
        put16(
            &mut p[26..],
            load(u64::from(a.peak_us), u64::from(a.peak_frames)),
        );
        p
    }
    fn start_flash(&mut self, s: &mut State, op: u8, id: u16, action: Action) -> Option<Reply> {
        let job = self.job.take().expect("one flash token");
        let candidate = if matches!(action, Action::FactoryReset) {
            job.reset = true;
            None
        } else {
            match s.controller.store.prepare(action) {
                Ok(plan) => {
                    job.image = plan.image;
                    job.bank = plan.bank;
                    job.reset = false;
                    Some(plan.store)
                }
                Err(e) => {
                    self.job = Some(job);
                    return Some(s.controller.flash_complete(op, id, action, Err(e)));
                }
            }
        };
        self.pending = Some(PendingFlash {
            op,
            id,
            action,
            candidate,
        });
        SHARED.flash_busy.store(true, Ordering::Release);
        s.stream.generation = s.stream.generation.wrapping_add(1);
        let mut paused = s.stream;
        paused.alternate = 0;
        SHARED.publish(paused);
        self.clear_spectrum();
        self.flash_tx.enqueue(job).ok().expect("one flash job");
        None
    }
    /// Housekeeping and analysis advance while the selected HID report is in flight.
    /// Responses and meter/spectrum report selection still happen in tick(),
    /// preserving response > meter > spectrum priority and immutable chunks.
    pub fn maintain_pending(&mut self, state: &RefCell<State>, now: u64) -> bool {
        if SHARED.flash_busy.load(Ordering::Acquire) {
            return false;
        }
        let mut s = state.borrow_mut();
        self.maintain(&mut s, now);
        let active = SHARED.stream().alternate != 0;
        self.spectrum_step(&mut s, now, active);
        self.spectrum_enabled && active
    }
    fn spectrum_step(&mut self, s: &mut State, now: u64, active: bool) {
        if self.spectrum_enabled
            && (s.controller.spectrum_deadline == 0 || now >= s.controller.spectrum_deadline)
        {
            self.spectrum.stop();
            self.spectrum_enabled = false;
            self.sending = None;
            self.waiting = None;
            s.controller.spectrum_deadline = 0;
        }
        if self.spectrum_enabled && active {
            self.spectrum
                .slice(|| embassy_time::Instant::now().as_micros() as u32);
            if let Some(frame) = self.spectrum.take() {
                self.waiting = Some(frame);
            }
        }
        SHARED
            .spectrum_us
            .store(self.spectrum.max_slice_us, Ordering::Relaxed);
        SHARED
            .spectrum_frames
            .store(self.spectrum.completed, Ordering::Relaxed);
        SHARED
            .spectrum_drops
            .store(self.spectrum.dropped, Ordering::Relaxed);
    }
    fn maintain(&mut self, s: &mut State, now: u64) -> bool {
        let stream = SHARED.stream();
        let active = stream.alternate != 0 && !SHARED.flash_busy.load(Ordering::Acquire);
        if stream.generation != self.stream_generation {
            if self.streaming {
                self.duration = ((now - self.started) / 1000).min(u64::from(u32::MAX)) as u32;
            }
            self.stream_generation = stream.generation;
            self.streaming = active;
            if active {
                self.started = now;
                self.duration = 0;
                self.starts = self.starts.saturating_add(1);
            }
            self.clear_spectrum();
        }
        let enabled = s
            .controller
            .maintain(now, |runtime| self.configs.enqueue(runtime).is_ok());
        let epoch = s.controller.meter.epoch;
        if epoch != self.meter_epoch {
            self.meter = Metrics::default();
            self.meter_epoch = epoch;
        }
        SHARED.meter_epoch.store(epoch, Ordering::Release);
        SHARED.meter_enabled.store(enabled, Ordering::Release);
        for _ in 0..16 {
            let Some(block) = self.meters.dequeue() else {
                break;
            };
            if enabled && block.epoch == epoch {
                self.meter.merge(block.metrics);
            }
        }
        enabled
    }
    pub fn tick(&mut self, state: &RefCell<State>, now: u64) -> Option<Reply> {
        let mut s = state.borrow_mut();
        let stream = SHARED.stream();
        let active = stream.alternate != 0 && !SHARED.flash_busy.load(Ordering::Acquire);
        let enabled = self.maintain(&mut s, now);
        if let Some(job) = self.flash_rx.dequeue() {
            let pending = self.pending.take().expect("pending flash");
            let result = job.result;
            self.job = Some(job);
            // The completion token proves ROM/XIP programming has finished.
            SHARED.flash_busy.store(false, Ordering::Release);
            let store = match result {
                Ok(()) => match pending.candidate {
                    Some(store) => Ok(store),
                    None => Store::load(&mut crate::flash::SettingsFlash),
                },
                Err(error) => {
                    self.flash_failures = self.flash_failures.saturating_add(1);
                    if matches!(pending.action, Action::FactoryReset)
                        && let Ok(store) = Store::load(&mut crate::flash::SettingsFlash)
                    {
                        s.controller.store = store;
                    }
                    Err(error)
                }
            };
            if matches!(pending.action, Action::FactoryReset) && store.is_ok() {
                s.stream.set_alt(0);
            }
            s.stream.generation = s.stream.generation.wrapping_add(1);
            SHARED.publish(s.stream);
            self.clear_spectrum();
            return Some(s.controller.flash_complete(
                pending.op,
                pending.id,
                pending.action,
                store,
            ));
        }
        if let Some(command) = s.command.take() {
            let info = self.status();
            let telemetry = self.telemetry(&s, now);
            let reply = s.controller.handle(&command, now, info, &telemetry);
            match reply.effect {
                Effect::Flash(action) => {
                    let packet = Packet::decode(&command).unwrap();
                    return self.start_flash(&mut s, packet.opcode, packet.request, action);
                }
                Effect::SpectrumStart => {
                    self.spectrum.start();
                    self.spectrum_enabled = true;
                    self.sending = None;
                    self.waiting = None;
                    self.chunk = 0;
                }
                Effect::SpectrumStop | Effect::Restart | Effect::Bootsel => {
                    self.spectrum.stop();
                    self.spectrum_enabled = false;
                    self.sending = None;
                    self.waiting = None;
                }
                Effect::None => {}
            }
            return Some(reply);
        }
        if SHARED.flash_busy.load(Ordering::Acquire) {
            return None;
        }
        self.spectrum_step(&mut s, now, active);
        if enabled && now >= s.controller.meter.next {
            s.controller.meter.sequence = s.controller.meter.sequence.wrapping_add(1);
            let bytes = protocol::response(
                0x33,
                0,
                Status::Ok,
                &self.meter.encode(s.controller.meter.sequence),
            )
            .unwrap();
            self.meter = Metrics::default();
            s.controller.meter.next = now + u64::from(s.controller.meter.interval) * 1000;
            self.meter_reports = self.meter_reports.saturating_add(1);
            return Some(Reply {
                bytes,
                effect: Effect::None,
            });
        }
        if self.spectrum_enabled && active {
            if self.sending.is_none() {
                self.sending = self.waiting.take();
                self.chunk = 0;
            }
            if let Some(frame) = &self.sending {
                let (p, n) = frame.chunk(self.chunk).unwrap();
                let bytes = protocol::response(0x37, 0, Status::Ok, &p[..n]).unwrap();
                self.chunk += 1;
                if self.chunk == 6 {
                    self.sending = None;
                }
                return Some(Reply {
                    bytes,
                    effect: Effect::None,
                });
            }
        }
        None
    }
}
