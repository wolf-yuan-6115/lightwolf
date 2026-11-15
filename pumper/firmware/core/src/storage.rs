//! Schema-5 alternating flash banks. Reads/migrations never write flash.
use crate::config::{Config, Crossfeed, Output, put16, put32, u16le, u32le};
pub const REGION: usize = 8192;
pub const BANK: usize = 4096;
pub const PAGE: usize = 256;
pub const USED: usize = 13 * PAGE;
const BANK_MAGIC: u32 = 0x32424c50;
const PROFILE_MAGIC: u32 = 0x32465250;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Io,
    Corrupt,
    Invalid,
}
pub trait Flash {
    fn read(&mut self, offset: usize, out: &mut [u8]) -> Result<(), Error>;
    fn erase(&mut self, offset: usize, len: usize) -> Result<(), Error>;
    fn program(&mut self, offset: usize, data: &[u8]) -> Result<(), Error>;
}
pub fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for b in bytes {
        crc ^= u32::from(*b);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb88320u32.wrapping_mul(crc & 1));
        }
    }
    !crc
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Profile {
    pub generation: u32,
    pub config: Config,
}
#[derive(Clone, Debug, PartialEq)]
pub struct Store {
    pub profiles: [Option<Profile>; 10],
    pub default: u8,
    pub generation: u32,
    pub current_bank: Option<u8>,
    pub crossfeed: Crossfeed,
    pub output: Output,
}
impl Default for Store {
    fn default() -> Self {
        Self {
            profiles: [None; 10],
            default: 0,
            generation: 0,
            current_bank: None,
            crossfeed: Crossfeed::DEFAULT,
            output: Output::DEFAULT,
        }
    }
}
#[derive(Clone, Copy, Debug)]
// Owned snapshots keep deferred saves allocation-free and independent of later edits.
#[allow(clippy::large_enum_variant)]
pub enum Action {
    Save(u8, Config, u32),
    Default(u8),
    Delete(u8),
    Crossfeed(Crossfeed),
    Output(Output),
    FactoryReset,
}
pub struct Prepared {
    pub store: Store,
    pub image: [u8; BANK],
    pub bank: u8,
}
impl Store {
    pub fn mask(&self) -> u16 {
        self.profiles
            .iter()
            .enumerate()
            .fold(0, |m, (i, p)| m | if p.is_some() { 1 << i } else { 0 })
    }
    pub fn boot_profile(&self) -> Option<Profile> {
        self.profiles[self.default as usize]
    }
    fn parse(b: &[u8; BANK], bank: u8) -> Option<Self> {
        let schema = u16le(&b[4..]);
        let mask = u16le(&b[12..]);
        if u32le(b) != BANK_MAGIC
            || !(2..=5).contains(&schema)
            || b[6] != 10
            || b[7] >= 10
            || b[14] != 0
            || b[15] != 0
            || mask & !1023 != 0
            || u32le(&b[16..]) != crc32(&b[..16])
        {
            return None;
        }
        let mut s = Self {
            default: b[7],
            generation: u32le(&b[8..]),
            current_bank: Some(bank),
            ..Self::default()
        };
        if schema >= 3 {
            let p = &b[11 * PAGE..];
            if u32le(p) != 0x33464350 || u32le(&p[12..]) != crc32(&p[..12]) {
                return None;
            }
            s.crossfeed = Crossfeed::decode(&p[4..12])?;
        }
        if schema >= 4 {
            let p = &b[12 * PAGE..];
            if u32le(p) != 0x3454554f || u32le(&p[12..]) != crc32(&p[..12]) {
                return None;
            }
            s.output = Output::decode(&p[4..12])?;
        }
        if schema < 5 {
            s.output.flags &= !0x10;
        }
        for i in 0..10 {
            if mask & (1 << i) == 0 {
                continue;
            }
            let p = &b[(i + 1) * PAGE..];
            if u32le(p) != PROFILE_MAGIC
                || u16le(&p[4..]) != schema
                || p[6] != i as u8
                || p[7] != 0xa5
                || u16le(&p[12..]) != 208
                || u32le(&p[16..]) != crc32(&p[32..240])
            {
                return None;
            }
            s.profiles[i] = Some(Profile {
                generation: u32le(&p[8..]),
                config: Config::from_record(&p[32..240])?,
            });
        }
        if s.profiles[s.default as usize].is_none() {
            s.default = s.profiles.iter().position(Option::is_some).unwrap_or(0) as u8;
        }
        Some(s)
    }
    pub fn load(flash: &mut impl Flash) -> Result<Self, Error> {
        let mut bank = [0; BANK];
        let mut chosen: Option<Self> = None;
        for i in 0..2 {
            flash.read(i * BANK, &mut bank)?;
            if let Some(s) = Self::parse(&bank, i as u8)
                && chosen
                    .as_ref()
                    .is_none_or(|old| (s.generation.wrapping_sub(old.generation) as i32) > 0)
            {
                chosen = Some(s);
            }
        }
        if let Some(s) = chosen {
            return Ok(s);
        }
        let mut s = Self::default(); // bank now contains the final sector / legacy record.
        if u32le(&bank) == 0x31514550
            && u16le(&bank[4..]) == 1
            && u16le(&bank[6..]) == 208
            && u32le(&bank[12..]) == crc32(&bank[16..224])
            && let Some(config) = Config::from_record(&bank[16..224])
        {
            s.profiles[0] = Some(Profile {
                generation: u32le(&bank[8..]),
                config,
            });
        }
        Ok(s)
    }
    pub fn prepare(&self, action: Action) -> Result<Prepared, Error> {
        let mut s = self.clone();
        match action {
            Action::Save(i, c, g) => {
                if i >= 10 || !c.valid() {
                    return Err(Error::Invalid);
                }
                if s.profiles[s.default as usize].is_none() {
                    s.default = i;
                }
                s.profiles[i as usize] = Some(Profile {
                    generation: g,
                    config: c,
                });
            }
            Action::Default(i) => {
                if i >= 10 || s.profiles[i as usize].is_none() {
                    return Err(Error::Invalid);
                }
                s.default = i;
            }
            Action::Delete(i) => {
                if i >= 10 || s.profiles[i as usize].is_none() {
                    return Err(Error::Invalid);
                }
                s.profiles[i as usize] = None;
                if s.default == i {
                    s.default = s.profiles.iter().position(Option::is_some).unwrap_or(0) as u8;
                }
            }
            Action::Crossfeed(c) => {
                if !c.valid() {
                    return Err(Error::Invalid);
                }
                s.crossfeed = c;
            }
            Action::Output(c) => {
                if !c.valid() {
                    return Err(Error::Invalid);
                }
                s.output = c;
            }
            Action::FactoryReset => return Err(Error::Invalid),
        }
        if s.mask() == 0 {
            s.default = 0;
        }
        let bank = if self.current_bank == Some(0) { 1 } else { 0 };
        s.current_bank = Some(bank);
        s.generation = s.generation.wrapping_add(1);
        let mut b = [0xff; BANK];
        for (i, p) in s.profiles.iter().enumerate() {
            if let Some(p) = p {
                let r = &mut b[(i + 1) * PAGE..(i + 2) * PAGE];
                put32(r, PROFILE_MAGIC);
                put16(&mut r[4..], 5);
                r[6] = i as u8;
                r[7] = 0xa5;
                put32(&mut r[8..], p.generation);
                put16(&mut r[12..], 208);
                r[32..240].copy_from_slice(&p.config.record());
                let crc = crc32(&r[32..240]);
                put32(&mut r[16..], crc);
            }
        }
        for (page, magic, data) in [
            (11, 0x33464350, s.crossfeed.encode()),
            (12, 0x3454554f, s.output.encode()),
        ] {
            let r = &mut b[page * PAGE..];
            put32(r, magic);
            r[4..12].copy_from_slice(&data);
            let crc = crc32(&r[..12]);
            put32(&mut r[12..], crc);
        }
        put32(&mut b, BANK_MAGIC);
        put16(&mut b[4..], 5);
        b[6] = 10;
        b[7] = s.default;
        put32(&mut b[8..], s.generation);
        put16(&mut b[12..], s.mask());
        b[14] = 0;
        b[15] = 0;
        let crc = crc32(&b[..16]);
        put32(&mut b[16..], crc);
        Ok(Prepared {
            store: s,
            image: b,
            bank,
        })
    }
    pub fn transact(&mut self, flash: &mut impl Flash, action: Action) -> Result<(), Error> {
        if matches!(action, Action::FactoryReset) {
            let result = factory_reset(flash);
            *self = Self::load(flash)?;
            return result;
        }
        let plan = self.prepare(action)?;
        commit(flash, plan.bank, &plan.image)?;
        *self = plan.store;
        Ok(())
    }
}
/// Pages are read back before the header is programmed. A failed data page cannot
/// become a committed bank even if power disappears immediately after the header.
pub fn commit(flash: &mut impl Flash, bank: u8, image: &[u8; BANK]) -> Result<(), Error> {
    if bank > 1 {
        return Err(Error::Invalid);
    }
    let base = bank as usize * BANK;
    flash.erase(base, BANK)?;
    let mut readback = [0; PAGE];
    for page in 1..13 {
        let start = page * PAGE;
        flash.program(base + start, &image[start..start + PAGE])?;
        flash.read(base + start, &mut readback)?;
        if readback != image[start..start + PAGE] {
            return Err(Error::Corrupt);
        }
    }
    flash.program(base, &image[..PAGE])?;
    flash.read(base, &mut readback)?;
    if readback != image[..PAGE] {
        return Err(Error::Corrupt);
    }
    Ok(())
}
pub fn factory_reset(flash: &mut impl Flash) -> Result<(), Error> {
    flash.erase(0, REGION)?;
    let mut b = [0; PAGE];
    for p in 0..REGION / PAGE {
        flash.read(p * PAGE, &mut b)?;
        if b.iter().any(|v| *v != 0xff) {
            return Err(Error::Corrupt);
        }
    }
    Ok(())
}
