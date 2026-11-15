pub const REPORT_SIZE: usize = 64;
pub const VERSION: u8 = 1;
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Status {
    Ok = 0,
    InvalidPacket = 1,
    InvalidCommand = 2,
    InvalidLength = 3,
    InvalidIndex = 4,
    OutOfRange = 5,
    Busy = 6,
    StorageError = 7,
}

#[derive(Debug)]
pub struct Packet<'a> {
    pub opcode: u8,
    pub request: u16,
    pub status: u8,
    pub payload: &'a [u8],
}
impl<'a> Packet<'a> {
    pub fn decode(report: &'a [u8]) -> Result<Self, Status> {
        if report.len() != 64 || report[..3] != [b'P', b'E', VERSION] || report[6] > 56 {
            return Err(Status::InvalidPacket);
        }
        Ok(Self {
            opcode: report[3],
            request: u16::from_le_bytes([report[4], report[5]]),
            status: report[7],
            payload: &report[8..8 + usize::from(report[6])],
        })
    }
}

pub fn response(
    opcode: u8,
    request: u16,
    status: Status,
    payload: &[u8],
) -> Result<[u8; 64], Status> {
    if payload.len() > 56 {
        return Err(Status::InvalidLength);
    }
    let mut report = [0; 64];
    report[..4].copy_from_slice(&[b'P', b'E', VERSION, opcode | 0x80]);
    report[4..6].copy_from_slice(&request.to_le_bytes());
    report[6] = payload.len() as u8;
    report[7] = status as u8;
    report[8..8 + payload.len()].copy_from_slice(payload);
    Ok(report)
}

pub fn milli(value: f32) -> i32 {
    libm::roundevenf(value * 1000.0) as i32
}

#[derive(Debug, Eq, PartialEq)]
pub enum Admission {
    Ready,
    Busy,
    Malformed,
}
/// Match the C transport boundary: busy reports are dropped first; malformed
/// envelopes count as malformed without generating a protocol response.
pub fn admit(report: &[u8], busy: bool) -> Admission {
    if busy {
        Admission::Busy
    } else if Packet::decode(report).is_err() {
        Admission::Malformed
    } else {
        Admission::Ready
    }
}
