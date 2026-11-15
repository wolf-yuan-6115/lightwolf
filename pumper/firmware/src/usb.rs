use crate::shared::SHARED;
use core::{cell::RefCell, sync::atomic::Ordering};
use embassy_usb::{
    Handler,
    control::{InResponse, OutResponse, Recipient, Request, RequestType},
    types::{InterfaceNumber, StringIndex},
};
use pumper_core::{
    descriptors::*,
    usb::{Host, HostDetection, Stream, Suspension},
};

#[cfg(feature = "diagnostics")]
include!(concat!(env!("OUT_DIR"), "/build_id.rs"));

pub struct State {
    pub controller: pumper_core::controller::Controller,
    pub malformed: u32,
    pub busy: u32,
    pub stream: Stream,
    suspension: Suspension,
    pub host: HostDetection,
    pub volume: i16,
    pub mute: bool,
    pub command: Option<[u8; 64]>,
    pub reset_pending: bool,
    pub activity: pumper_core::activity::Activity,
}
impl State {
    pub fn new(store: pumper_core::storage::Store) -> Self {
        Self {
            controller: pumper_core::controller::Controller::new(store),
            malformed: 0,
            busy: 0,
            stream: Stream::default(),
            suspension: Suspension::default(),
            host: HostDetection::default(),
            volume: 0,
            mute: false,
            command: None,
            reset_pending: false,
            activity: Default::default(),
        }
    }

    fn stop(&mut self) {
        self.suspension.cancel();
        self.command = None;
        self.stream.set_alt(0);
        self.controller.disconnected();
        SHARED.publish(self.stream);
    }
    pub fn enqueue(&mut self, data: &[u8]) {
        self.activity
            .notify(embassy_time::Instant::now().as_micros() as u32);
        use pumper_core::protocol::{Admission, admit};
        let busy = self.command.is_some() || self.controller.flash_pending || self.reset_pending;
        match admit(data, busy) {
            Admission::Busy => self.busy = self.busy.saturating_add(1),
            Admission::Malformed => self.malformed = self.malformed.saturating_add(1),
            Admission::Ready => self.command = Some(data.try_into().unwrap()),
        }
    }

    fn gain(&mut self) {
        self.controller.volume = self.volume;
        self.controller.mute = self.mute;
        self.controller.publish_controls();
    }
}
pub struct Control<'a>(pub &'a RefCell<State>);

#[cfg(feature = "diagnostics")]
const DIAGNOSTIC_HID: [u8; 29] = [
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x09, 0x02, 0x81, 0x02, 0x09, 0x03, 0x91, 0x02, 0x09, 0x04, 0xb1, 0x02, 0xc0,
];

const CONFIG_LEN: usize = DESC_CONFIGURATION.len();
const fn configuration(source: &[u8]) -> [u8; CONFIG_LEN] {
    let mut result = [0; CONFIG_LEN];
    let mut i = 0;
    while i < CONFIG_LEN {
        result[i] = source[i];
        i += 1;
    }
    #[cfg(feature = "diagnostics")]
    {
        // HID descriptor is the final class descriptor, followed by its two endpoints.
        let mut pos = 0;
        while pos < CONFIG_LEN {
            if result[pos + 1] == 0x21 {
                result[pos + 7] = 29;
                result[pos + 8] = 0;
            }
            pos += result[pos] as usize;
        }
    }
    result
}
static CONFIG: [u8; CONFIG_LEN] = configuration(DESC_CONFIGURATION);
static CONFIG_MAC: [u8; CONFIG_LEN] = configuration(DESC_CONFIGURATION_MACOS);

impl Handler for Control<'_> {
    fn reset(&mut self) {
        let mut s = self.0.borrow_mut();
        s.stop();
        s.host = HostDetection::default();
        s.activity = Default::default();
        SHARED.resets.fetch_add(1, Ordering::Relaxed);
    }
    fn configured(&mut self, configured: bool) {
        if !configured {
            self.0.borrow_mut().stop();
        }
    }
    fn suspended(&mut self, suspended: bool) {
        let mut state = self.0.borrow_mut();
        let State {
            stream, suspension, ..
        } = &mut *state;
        suspension.update(stream, suspended);
        SHARED.publish(*stream);
    }
    fn standard_descriptor(&mut self, req: Request) -> Option<&'static [u8]> {
        let dtype = (req.value >> 8) as u8;
        let mut s = self.0.borrow_mut();
        s.host.observe(dtype);
        match dtype {
            1 => Some(DESC_DEVICE),
            2 => Some(if s.host.host() == Host::Macos {
                &CONFIG_MAC
            } else {
                &CONFIG
            }),
            15 => Some(DESC_BOS),
            _ => None,
        }
    }
    fn get_string(&mut self, index: StringIndex, _: u16) -> Option<&str> {
        match u8::from(index) {
            4 => Some("LightWolf Pumper DAC"),
            5 => Some("Pumper EQ Control"),
            _ => None,
        }
    }
    fn accept_alternate_setting(&mut self, iface: InterfaceNumber, alt: u8) -> bool {
        u8::from(iface) != 1 || self.0.borrow().stream.accepts_alt(alt)
    }
    fn set_alternate_setting(&mut self, iface: InterfaceNumber, alt: u8) {
        if u8::from(iface) == 1 {
            let mut s = self.0.borrow_mut();
            s.stream.set_alt(alt);
            SHARED.publish(s.stream);
        }
    }
    fn control_in<'a>(&'a mut self, req: Request, buf: &'a mut [u8]) -> Option<InResponse<'a>> {
        if req.recipient == Recipient::Interface && req.index == 2 {
            if req.request_type == RequestType::Standard && req.request == 6 && req.value == 0x2200
            {
                #[cfg(feature = "diagnostics")]
                return Some(InResponse::Accepted(&DIAGNOSTIC_HID));
                #[cfg(not(feature = "diagnostics"))]
                return Some(InResponse::Accepted(DESC_HID_REPORT));
            }
            #[cfg(feature = "diagnostics")]
            if req.request_type == RequestType::Class && req.request == 1 && req.value == 0x0300 {
                buf[..64].fill(0);
                buf[..4].copy_from_slice(b"PDBG");
                buf[4] = 4;
                buf[6] = 64;
                buf[8..16].copy_from_slice(&BUILD_ID);
                let values = [
                    (16, SHARED.block_us.load(Ordering::Relaxed)),
                    (24, crate::SPECTRUM.dropped.load(Ordering::Relaxed)),
                    (28, SHARED.spectrum_drops.load(Ordering::Relaxed)),
                    (32, crate::diagnostics::CORE0_FREE.load(Ordering::Relaxed)),
                    (36, crate::diagnostics::CORE1_FREE.load(Ordering::Relaxed)),
                    (40, crate::shared::OWNERSHIP_FAULTS.load(Ordering::Relaxed)),
                    (44, SHARED.resets.load(Ordering::Relaxed)),
                    (48, SHARED.underruns.load(Ordering::Relaxed)),
                    (52, crate::hardware::clock_hz()),
                    (56, crate::flash::LAST_RESULT.load(Ordering::Relaxed) as u32),
                    (60, SHARED.spectrum_frames.load(Ordering::Relaxed)),
                ];
                for (offset, value) in values {
                    buf[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
                }
                let slice = SHARED.spectrum_us.load(Ordering::Relaxed).min(65535) as u16;
                let fifo = crate::SPECTRUM
                    .high_water
                    .load(Ordering::Relaxed)
                    .min(65535) as u16;
                buf[20..22].copy_from_slice(&slice.to_le_bytes());
                buf[22..24].copy_from_slice(&fifo.to_le_bytes());
                return Some(InResponse::Accepted(&buf[..64]));
            }
            if req.request_type == RequestType::Class && req.request == 1 && req.value == 0x0100 {
                buf[..64].fill(0);
                return Some(InResponse::Accepted(&buf[..64]));
            }
            return None;
        }
        if req.request_type != RequestType::Class {
            return None;
        }
        let Some((entity, selector)) = pumper_core::usb::audio_control_address(
            req.recipient == Recipient::Interface,
            req.index,
            req.value,
        ) else {
            return Some(InResponse::Rejected);
        };
        let s = self.0.borrow();
        let channel = req.value as u8;
        let n = match (entity, selector, req.request) {
            (4, 1, 1) => {
                buf[..4].copy_from_slice(&s.stream.rate.to_le_bytes());
                if req.length == 3 { 3 } else { 4 }
            }
            (4, 1, 2) => {
                buf[..2].copy_from_slice(&6u16.to_le_bytes());
                for (i, rate) in pumper_core::format::RATES.iter().enumerate() {
                    let p = 2 + i * 12;
                    buf[p..p + 4].copy_from_slice(&rate.to_le_bytes());
                    buf[p + 4..p + 8].copy_from_slice(&rate.to_le_bytes());
                    buf[p + 8..p + 12].fill(0);
                }
                74
            }
            (4, 2, 1) => {
                buf[0] = 1;
                1
            }
            (4, 2, 2) => {
                buf[..5].copy_from_slice(&[1, 0, 1, 1, 0]);
                5
            }
            (2, 1, 1) if channel == 0 && req.length == 1 => {
                buf[0] = u8::from(s.mute);
                1
            }
            (2, 2, 1) if channel == 0 && req.length == 2 => {
                buf[..2].copy_from_slice(&s.volume.to_le_bytes());
                2
            }
            (2, 2, 2) if channel == 0 && (req.length == 2 || req.length == 8) => {
                buf[..8].copy_from_slice(&[1, 0, 0, 206, 0, 0, 0, 1]);
                8
            }
            _ => return Some(InResponse::Rejected),
        };
        Some(InResponse::Accepted(&buf[..n]))
    }
    fn control_out(&mut self, req: Request, data: &[u8]) -> Option<OutResponse> {
        if req.request_type != RequestType::Class {
            return None;
        }
        if req.recipient != Recipient::Interface {
            return Some(OutResponse::Rejected);
        }
        if req.index == 2 {
            if req.request == 9 && req.value == 0x0200 {
                self.0.borrow_mut().enqueue(data);
                return Some(OutResponse::Accepted);
            }
            // SET_IDLE; no numbered reports.
            if req.request == 10 && req.value as u8 == 0 {
                return Some(OutResponse::Accepted);
            }
            return Some(OutResponse::Rejected);
        }
        let Some((entity, selector)) =
            pumper_core::usb::audio_control_address(true, req.index, req.value)
        else {
            return Some(OutResponse::Rejected);
        };
        let mut s = self.0.borrow_mut();
        let accepted = match (entity, selector, req.request) {
            (4, 1, 1) if data.len() == 3 || data.len() == 4 => {
                let mut bytes = [0; 4];
                bytes[..data.len()].copy_from_slice(data);
                let ok = s.stream.set_rate(u32::from_le_bytes(bytes));
                if ok {
                    SHARED.publish(s.stream);
                }
                ok
            }
            (2, 1, 1) if req.value as u8 == 0 && data.len() == 1 && data[0] <= 1 => {
                s.mute = data[0] != 0;
                s.gain();
                true
            }
            (2, 2, 1) if req.value as u8 == 0 && data.len() == 2 => {
                let value = i16::from_le_bytes([data[0], data[1]]);
                if !(-12800..=0).contains(&value) || value % 256 != 0 {
                    false
                } else {
                    s.volume = value;
                    s.gain();
                    true
                }
            }
            _ => false,
        };
        Some(if accepted {
            OutResponse::Accepted
        } else {
            OutResponse::Rejected
        })
    }
}
