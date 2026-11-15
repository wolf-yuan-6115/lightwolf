//! Core-0 command state machine. Persistence is an explicit deferred effect.
use crate::{
    config::*,
    protocol::{self, Packet, Status},
    storage::{Action, Store},
};
#[derive(Clone, Copy, Default)]
pub struct DeviceStatus {
    pub streaming: bool,
    pub rate: u32,
    pub bits: u8,
    pub applied: u32,
    pub underruns: u32,
    pub backpressure: u32,
    pub temperature: i32,
    pub clock: u32,
    pub dsp_us: u32,
    pub low_water: u32,
}
#[derive(Clone, Copy, Debug)]
// Owned snapshots keep deferred saves allocation-free and independent of later edits.
#[allow(clippy::large_enum_variant)]
pub enum Effect {
    None,
    Flash(Action),
    Restart,
    Bootsel,
    SpectrumStart,
    SpectrumStop,
}
pub struct Reply {
    pub bytes: [u8; 64],
    pub effect: Effect,
}
#[derive(Clone, Copy)]
pub struct Subscription {
    pub configured: bool,
    pub deadline: u64,
    pub next: u64,
    pub interval: u16,
    pub timeout: u16,
    pub sequence: u32,
    pub epoch: u32,
}
impl Default for Subscription {
    fn default() -> Self {
        Self {
            configured: false,
            deadline: 0,
            next: 0,
            interval: 40,
            timeout: 1250,
            sequence: 0,
            epoch: 0,
        }
    }
}
impl Subscription {
    pub fn active(&self, now: u64) -> bool {
        self.configured && now < self.deadline
    }
    pub fn stop(&mut self) {
        self.configured = false;
        self.deadline = 0;
        self.next = 0;
        self.epoch = self.epoch.wrapping_add(1);
    }
}
pub struct Controller {
    pub runtime: Runtime,
    pub saved: Config,
    pub saved_generation: u32,
    pub active_profile: u8,
    pub store: Store,
    pub volume: i16,
    pub mute: bool,
    pub meter: Subscription,
    pub spectrum_deadline: u64,
    pub flash_pending: bool,
    pub dirty_runtime: bool,
}
impl Controller {
    pub fn new(store: Store) -> Self {
        let profile = store.boot_profile();
        let runtime = Runtime {
            eq: profile.map_or(Config::FLAT, |p| p.config),
            generation: profile.map_or(1, |p| p.generation),
            crossfeed: store.crossfeed,
            output: store.output,
            ..Runtime::default()
        };
        Self {
            saved: runtime.eq,
            saved_generation: runtime.generation,
            active_profile: store.default,
            store,
            runtime,
            volume: 0,
            mute: false,
            meter: Subscription::default(),
            spectrum_deadline: 0,
            flash_pending: false,
            dirty_runtime: true,
        }
    }
    pub fn eq_dirty(&self) -> bool {
        self.runtime.eq != self.saved
    }
    pub fn publish_eq(&mut self, eq: Config) {
        self.runtime.eq = eq;
        self.runtime.generation = self.runtime.generation.wrapping_add(1);
        self.dirty_runtime = true;
    }
    pub fn publish_controls(&mut self) {
        let gain = if self.mute {
            0.0
        } else if self.runtime.output.flags & 16 != 0 || self.volume == 0 {
            1.0
        } else {
            libm::powf(10.0, f32::from(self.volume) / 5120.0)
        };
        self.runtime.gains = [gain; 2];
        self.runtime.controls_generation = self.runtime.controls_generation.wrapping_add(1);
        self.dirty_runtime = true;
    }
    pub fn disconnected(&mut self) {
        self.meter.stop();
        self.spectrum_deadline = 0;
    }
    /// Keep host controls and subscription expiry independent of HID delivery.
    /// A full configuration queue retains the latest snapshot for a later retry.
    pub fn maintain(&mut self, now: u64, mut publish: impl FnMut(Runtime) -> bool) -> bool {
        if self.dirty_runtime && publish(self.runtime) {
            self.dirty_runtime = false;
        }
        let enabled = self.meter.active(now);
        if !enabled && self.meter.deadline != 0 && now >= self.meter.deadline {
            self.meter.deadline = 0;
            self.meter.epoch = self.meter.epoch.wrapping_add(1);
        }
        enabled
    }
    fn state_record(&self, output: bool) -> [u8; 17] {
        let (a, b) = if output {
            (self.runtime.output.encode(), self.store.output.encode())
        } else {
            (
                self.runtime.crossfeed.encode(),
                self.store.crossfeed.encode(),
            )
        };
        let mut p = [0; 17];
        p[..8].copy_from_slice(&a);
        p[8..16].copy_from_slice(&b);
        p[16] = u8::from(a != b);
        p
    }
    pub fn status(&self, d: DeviceStatus) -> [u8; 48] {
        let mut p = [0; 48];
        p[..4].copy_from_slice(&[
            crate::FIRMWARE_VERSION.0,
            crate::FIRMWARE_VERSION.1,
            10,
            u8::from(d.streaming)
                | (u8::from(self.eq_dirty()) << 1)
                | (u8::from(self.runtime.eq.enabled) << 2),
        ]);
        for (offset, value) in [
            (4, d.rate),
            (8, self.runtime.generation),
            (12, self.saved_generation),
            (16, d.applied),
            (20, d.underruns),
            (24, d.backpressure),
            (28, d.temperature as u32),
            (32, d.clock),
            (36, d.dsp_us),
            (40, d.low_water),
        ] {
            put32(&mut p[offset..], value);
        }
        p[44] = d.bits;
        p
    }
    pub fn handle(
        &mut self,
        report: &[u8],
        now: u64,
        d: DeviceStatus,
        telemetry: &[u8; 56],
    ) -> Reply {
        let mut out = [0; 56];
        let mut effect = Effect::None;
        let decoded = Packet::decode(report);
        let (op, id) = if let Ok(ref p) = decoded {
            (p.opcode, p.request)
        } else {
            (
                report.get(3).copied().unwrap_or(0),
                if report.len() >= 6 {
                    u16le(&report[4..])
                } else {
                    0
                },
            )
        };
        let result = (|| -> Result<usize, Status> {
            let p = decoded?;
            if op & 0x80 != 0 || p.status != 0 {
                return Err(Status::InvalidPacket);
            }
            let data = p.payload;
            let expected = match op {
                0x04 | 0x22..=0x25 => 1,
                0x10 | 0x12 | 0x13 => 8,
                0x11 => 21,
                0x30 => 4,
                1..=3
                | 5..=9
                | 0x20
                | 0x21
                | 0x26
                | 0x27
                | 0x31
                | 0x32
                | 0x34..=0x36
                | 0x40..=0x42 => 0,
                _ => return Err(Status::InvalidCommand),
            };
            if data.len() != expected {
                return Err(Status::InvalidLength);
            }
            match op {
                1 | 2 => {
                    out[..48].copy_from_slice(&self.status(d));
                    Ok(48)
                }
                3 => {
                    out[..8].copy_from_slice(&self.runtime.eq.global());
                    Ok(8)
                }
                4 => {
                    let i = data[0] as usize;
                    if i >= 10 {
                        return Err(Status::InvalidIndex);
                    }
                    out[..21].copy_from_slice(&self.runtime.eq.bands[i].encode(i as u8));
                    Ok(21)
                }
                5 => {
                    out[0] = 10;
                    out[1] = self.active_profile;
                    out[2] = self.store.default;
                    put16(&mut out[4..], self.store.mask());
                    put32(&mut out[8..], self.store.generation);
                    Ok(12)
                }
                6 => {
                    put16(&mut out, self.volume as u16);
                    out[6] = u8::from(self.mute);
                    Ok(9)
                }
                7 | 8 => {
                    out[..17].copy_from_slice(&self.state_record(op == 8));
                    Ok(17)
                }
                9 => {
                    out.copy_from_slice(telemetry);
                    Ok(56)
                }
                0x10 => {
                    let eq = self.runtime.eq.with_global(data)?;
                    self.publish_eq(eq);
                    out[..8].copy_from_slice(&eq.global());
                    Ok(8)
                }
                0x11 => {
                    let (i, band) = Band::decode(data)?;
                    if i >= 10 {
                        return Err(Status::InvalidIndex);
                    }
                    let mut eq = self.runtime.eq;
                    eq.bands[i as usize] = band;
                    if !eq.valid() {
                        return Err(Status::OutOfRange);
                    }
                    self.publish_eq(eq);
                    out[..21].copy_from_slice(&band.encode(i));
                    Ok(21)
                }
                0x12 => {
                    self.runtime.crossfeed = Crossfeed::decode(data).ok_or(Status::OutOfRange)?;
                    self.publish_controls();
                    out[..17].copy_from_slice(&self.state_record(false));
                    Ok(17)
                }
                0x13 => {
                    self.runtime.output = Output::decode(data).ok_or(Status::OutOfRange)?;
                    self.publish_controls();
                    out[..17].copy_from_slice(&self.state_record(true));
                    Ok(17)
                }
                0x21 => {
                    self.publish_eq(Config::FLAT);
                    Ok(0)
                }
                0x22 => {
                    let i = data[0];
                    let profile = self
                        .store
                        .profiles
                        .get(i as usize)
                        .copied()
                        .flatten()
                        .ok_or(Status::InvalidIndex)?;
                    self.publish_eq(profile.config);
                    self.saved = profile.config;
                    self.saved_generation = self.runtime.generation;
                    self.active_profile = i;
                    out[0] = i;
                    put32(&mut out[4..], self.runtime.generation);
                    Ok(8)
                }
                0x20 | 0x23..=0x27 | 0x42 => {
                    if self.flash_pending {
                        return Err(Status::Busy);
                    }
                    let action = match op {
                        0x20 => Action::Save(
                            self.active_profile,
                            self.runtime.eq,
                            self.runtime.generation,
                        ),
                        0x23 => {
                            if data[0] >= 10 {
                                return Err(Status::InvalidIndex);
                            }
                            Action::Save(data[0], self.runtime.eq, self.runtime.generation)
                        }
                        0x24 | 0x25 => {
                            let i = data[0];
                            if i >= 10 || self.store.profiles[i as usize].is_none() {
                                return Err(Status::InvalidIndex);
                            }
                            if op == 0x24 {
                                Action::Default(i)
                            } else {
                                Action::Delete(i)
                            }
                        }
                        0x26 => Action::Crossfeed(self.runtime.crossfeed),
                        0x27 => Action::Output(self.runtime.output),
                        _ => Action::FactoryReset,
                    };
                    self.flash_pending = true;
                    effect = Effect::Flash(action);
                    Ok(0)
                }
                0x30 => {
                    let interval = u16le(data);
                    let timeout = u16le(&data[2..]);
                    if !(20..=250).contains(&interval)
                        || !(250..=5000).contains(&timeout)
                        || timeout <= interval
                    {
                        return Err(Status::OutOfRange);
                    }
                    self.meter = Subscription {
                        configured: true,
                        deadline: now + u64::from(timeout) * 1000,
                        next: 0,
                        interval,
                        timeout,
                        sequence: 0,
                        epoch: self.meter.epoch.wrapping_add(1),
                    };
                    out[..4].copy_from_slice(data);
                    Ok(4)
                }
                0x31 => {
                    if !self.meter.configured {
                        return Err(Status::InvalidPacket);
                    }
                    self.meter.deadline = now + u64::from(self.meter.timeout) * 1000;
                    Ok(0)
                }
                0x32 => {
                    self.meter.stop();
                    Ok(0)
                }
                0x34 => {
                    self.spectrum_deadline = now + 1_250_000;
                    effect = Effect::SpectrumStart;
                    Ok(0)
                }
                0x35 => {
                    if self.spectrum_deadline == 0 || now >= self.spectrum_deadline {
                        return Err(Status::InvalidCommand);
                    }
                    self.spectrum_deadline = now + 1_250_000;
                    Ok(0)
                }
                0x36 => {
                    self.spectrum_deadline = 0;
                    effect = Effect::SpectrumStop;
                    Ok(0)
                }
                0x40 | 0x41 => {
                    if self.flash_pending {
                        return Err(Status::Busy);
                    }
                    self.disconnected();
                    effect = if op == 0x40 {
                        Effect::Restart
                    } else {
                        Effect::Bootsel
                    };
                    Ok(0)
                }
                _ => Err(Status::InvalidCommand),
            }
        })();
        let (status, len) = match result {
            Ok(n) => (Status::Ok, n),
            Err(e) => {
                effect = Effect::None;
                (e, 0)
            }
        };
        Reply {
            bytes: protocol::response(op, id, status, &out[..len]).unwrap(),
            effect,
        }
    }
    pub fn flash_complete(
        &mut self,
        op: u8,
        id: u16,
        action: Action,
        result: Result<Store, crate::storage::Error>,
    ) -> Reply {
        self.flash_pending = false;
        let mut out = [0; 17];
        let mut effect = Effect::None;
        let (status, len) = match result {
            Err(_) => (Status::StorageError, 0),
            Ok(store) => {
                self.store = store;
                match action {
                    Action::Save(i, c, g) => {
                        self.saved = c;
                        self.saved_generation = g;
                        self.active_profile = i;
                        out[0] = i;
                        put32(&mut out[4..], g);
                        (Status::Ok, 8)
                    }
                    Action::Default(i) | Action::Delete(i) => {
                        out[0] = i;
                        put32(&mut out[4..], self.store.generation);
                        (Status::Ok, 8)
                    }
                    Action::Crossfeed(_) => {
                        out = self.state_record(false);
                        (Status::Ok, 17)
                    }
                    Action::Output(_) => {
                        out = self.state_record(true);
                        (Status::Ok, 17)
                    }
                    Action::FactoryReset => {
                        self.disconnected();
                        effect = Effect::Restart;
                        (Status::Ok, 0)
                    }
                }
            }
        };
        Reply {
            bytes: protocol::response(op, id, status, &out[..len]).unwrap(),
            effect,
        }
    }
}
