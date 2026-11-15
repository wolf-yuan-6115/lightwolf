use pumper_core::{config::*, storage::*};
#[derive(Clone)]
struct Memory {
    bytes: [u8; REGION],
    operations: usize,
    fail: Option<usize>,
    corrupt_data: bool,
    partial_erase: Option<usize>,
    commits: usize,
}
impl Default for Memory {
    fn default() -> Self {
        Self {
            bytes: [0xff; REGION],
            operations: 0,
            fail: None,
            corrupt_data: false,
            partial_erase: None,
            commits: 0,
        }
    }
}
impl Memory {
    fn operation(&mut self) -> Result<(), Error> {
        let i = self.operations;
        self.operations += 1;
        if self.fail == Some(i) {
            Err(Error::Io)
        } else {
            Ok(())
        }
    }
}
impl Flash for Memory {
    fn read(&mut self, offset: usize, out: &mut [u8]) -> Result<(), Error> {
        self.operation()?;
        out.copy_from_slice(&self.bytes[offset..offset + out.len()]);
        Ok(())
    }
    fn erase(&mut self, offset: usize, len: usize) -> Result<(), Error> {
        self.operation()?;
        let erased = self.partial_erase.take().unwrap_or(len).min(len);
        self.bytes[offset..offset + erased].fill(0xff);
        if erased < len { Err(Error::Io) } else { Ok(()) }
    }
    fn program(&mut self, offset: usize, data: &[u8]) -> Result<(), Error> {
        self.operation()?;
        assert!(offset.is_multiple_of(PAGE));
        for (dst, src) in self.bytes[offset..offset + data.len()].iter_mut().zip(data) {
            assert_eq!(*dst & *src, *src);
            *dst = *src;
        }
        if offset.is_multiple_of(BANK) {
            self.commits += 1;
        } else if self.corrupt_data {
            self.bytes[offset] ^= 1;
        }
        Ok(())
    }
}
#[test]
fn interrupted_writes_keep_last_committed_bank() {
    let mut base = Memory::default();
    let mut s = Store::load(&mut base).unwrap();
    s.transact(&mut base, Action::Save(3, Config::FLAT, 7))
        .unwrap();
    let old = s.clone();
    // erase + twelve program/readback pairs + header program/readback.
    for fail in 0..27 {
        let mut mem = base.clone();
        mem.operations = 0;
        mem.fail = Some(fail);
        let mut store = old.clone();
        assert!(
            store
                .transact(
                    &mut mem,
                    Action::Output(Output {
                        flags: 16,
                        ..Output::DEFAULT
                    })
                )
                .is_err()
        );
        assert_eq!(store, old);
        mem.fail = None;
        let loaded = Store::load(&mut mem).unwrap();
        assert_eq!(loaded.profiles, old.profiles);
        if fail < 26 {
            assert_eq!(loaded.output, old.output);
        } else {
            assert_eq!(loaded.output.flags, 16);
        }
    }
    let mut mem = base.clone();
    mem.corrupt_data = true;
    let commits = mem.commits;
    assert_eq!(
        s.transact(&mut mem, Action::Default(3)),
        Err(Error::Corrupt)
    );
    assert_eq!(mem.commits, commits);
}
#[test]
fn independent_settings_defaults_and_delete() {
    let mut m = Memory::default();
    let mut s = Store::load(&mut m).unwrap();
    s.transact(
        &mut m,
        Action::Crossfeed(Crossfeed {
            mode: 3,
            ..Crossfeed::DEFAULT
        }),
    )
    .unwrap();
    assert!(Store::load(&mut m).unwrap().boot_profile().is_none());
    s.transact(&mut m, Action::Save(7, Config::FLAT, 3))
        .unwrap();
    assert_eq!(s.default, 7);
    s.transact(&mut m, Action::Save(2, Config::FLAT, 9))
        .unwrap();
    assert_eq!(s.default, 7);
    s.transact(&mut m, Action::Delete(7)).unwrap();
    assert_eq!(s.default, 2);
    s.transact(&mut m, Action::Delete(2)).unwrap();
    assert!(s.boot_profile().is_none());
    assert_eq!(Store::load(&mut m).unwrap().crossfeed.mode, 3);
    s.transact(&mut m, Action::FactoryReset).unwrap();
    assert!(m.bytes.iter().all(|b| *b == 0xff));
    assert_eq!(s, Store::default());
}
#[test]
fn legacy_and_old_banks_migrate_without_programming() {
    let mut m = Memory::default();
    let b = &mut m.bytes[BANK..];
    put32(b, 0x31514550);
    put16(&mut b[4..], 1);
    put16(&mut b[6..], 208);
    put32(&mut b[8..], 22);
    b[16..224].copy_from_slice(&Config::FLAT.record());
    let crc = crc32(&b[16..224]);
    put32(&mut b[12..], crc);
    let s = Store::load(&mut m).unwrap();
    assert_eq!(s.boot_profile().unwrap().generation, 22);
    assert_eq!(m.commits, 0);
    let mut s = Store::default();
    s.transact(
        &mut m,
        Action::Output(Output {
            flags: 16,
            ..Output::DEFAULT
        }),
    )
    .unwrap();
    let b = &mut m.bytes[..BANK];
    put16(&mut b[4..], 4);
    let crc = crc32(&b[..16]);
    put32(&mut b[16..], crc);
    let commits = m.commits;
    let s = Store::load(&mut m).unwrap();
    assert_eq!(s.output.flags, 0);
    assert_eq!(m.commits, commits);
}
#[test]
fn generation_wrap_and_invalid_candidate_do_not_erase() {
    let mut m = Memory::default();
    let mut s = Store {
        generation: u32::MAX - 1,
        ..Store::default()
    };
    s.transact(&mut m, Action::Save(0, Config::FLAT, 1))
        .unwrap();
    s.transact(&mut m, Action::Save(1, Config::FLAT, 2))
        .unwrap();
    assert_eq!(Store::load(&mut m).unwrap().generation, 0);
    let old = m.bytes;
    assert_eq!(s.transact(&mut m, Action::Delete(9)), Err(Error::Invalid));
    assert_eq!(m.bytes, old);
}
#[test]
fn schema_five_images_are_byte_exact_with_c() {
    let reference = include_bytes!("../../reference/golden/storage.bin");
    let mut mem = Memory::default();
    let mut s = Store::default();
    let mut c = Config {
        preamp: -3.0,
        ..Config::FLAT
    };
    s.transact(&mut mem, Action::Save(3, c, 7)).unwrap();
    s.transact(
        &mut mem,
        Action::Crossfeed(Crossfeed {
            mode: 3,
            strength: 3000,
            cutoff: 900,
            delay: 350,
        }),
    )
    .unwrap();
    s.transact(
        &mut mem,
        Action::Output(Output {
            flags: 16,
            balance: -1500,
            width: 13000,
        }),
    )
    .unwrap();
    c.preamp = -6.0;
    s.transact(&mut mem, Action::Save(1, c, 11)).unwrap();
    s.transact(&mut mem, Action::Delete(3)).unwrap();
    assert_eq!(mem.bytes.as_slice(), reference);
}

#[test]
fn interrupted_factory_reset_is_non_atomic_and_never_programs_a_replacement() {
    let mut base = Memory::default();
    let mut original = Store::default();
    original
        .transact(&mut base, Action::Save(1, Config::FLAT, 1))
        .unwrap();
    original
        .transact(
            &mut base,
            Action::Save(
                2,
                Config {
                    preamp: -3.0,
                    ..Config::FLAT
                },
                2,
            ),
        )
        .unwrap();
    assert_eq!(original.current_bank, Some(1));
    for cut in (0..=REGION)
        .step_by(PAGE)
        .chain([1, BANK - 1, BANK + 1, REGION - 1])
    {
        let mut memory = base.clone();
        memory.partial_erase = Some(cut);
        let mut store = original.clone();
        let result = store.transact(&mut memory, Action::FactoryReset);
        assert_eq!(
            result,
            if cut == REGION {
                Ok(())
            } else {
                Err(Error::Io)
            }
        );
        assert_eq!(memory.commits, base.commits);
        assert!(memory.bytes[..cut].iter().all(|v| *v == 255));
        assert_eq!(&memory.bytes[cut..], &base.bytes[cut..]);
        let reloaded = Store::load(&mut memory).unwrap();
        assert_eq!(store, reloaded);
        if cut <= BANK {
            assert_eq!(store.profiles, original.profiles);
        } else {
            assert_eq!(store.mask(), 0);
        }
    }
    // Failure at each verification page after erase must still report failure.
    for fail in 1..=REGION / PAGE {
        let mut memory = base.clone();
        memory.operations = 0;
        memory.fail = Some(fail);
        assert!(factory_reset(&mut memory).is_err());
        assert_eq!(memory.commits, base.commits);
    }
}

#[test]
fn schemas_two_three_and_four_migrate_profiles_without_writes() {
    let mut source = Store::default();
    source.profiles[7] = Some(Profile {
        generation: 93,
        config: Config {
            preamp: -3.0,
            ..Config::FLAT
        },
    });
    source.default = 7;
    source.crossfeed.mode = 3;
    let prepared = source
        .prepare(Action::Output(Output {
            flags: 16,
            width: 13000,
            ..Output::DEFAULT
        }))
        .unwrap();
    for schema in 2..=4 {
        let mut memory = Memory::default();
        let bank = &mut memory.bytes[..BANK];
        bank.copy_from_slice(&prepared.image);
        put16(&mut bank[4..], schema);
        put16(&mut bank[8 * PAGE + 4..], schema);
        let crc = crc32(&bank[..16]);
        put32(&mut bank[16..], crc);
        let original = memory.bytes;
        let loaded = Store::load(&mut memory).unwrap();
        assert_eq!(loaded.default, 7);
        assert_eq!(loaded.boot_profile(), source.boot_profile());
        assert_eq!(loaded.crossfeed.mode, if schema == 2 { 0 } else { 3 });
        assert_eq!(loaded.output.flags, 0);
        assert_eq!(loaded.output.width, if schema < 4 { 10000 } else { 13000 });
        assert_eq!(memory.bytes, original);
        assert_eq!(memory.commits, 0);
    }
}
