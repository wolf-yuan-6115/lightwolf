//! Host-side ELF validation and UF2 generation. No flashing is performed here.
use std::{env, fs, path::Path};
const FLASH: u32 = 0x10000000;
const SETTINGS: u32 = 0x101fe000;
const RAM: u32 = 0x20000000;
const RAM_END: u32 = 0x20082000;
fn u32_at(data: &[u8], offset: usize) -> Result<u32, String> {
    Ok(u32::from_le_bytes(
        data.get(offset..offset + 4)
            .ok_or("truncated ELF")?
            .try_into()
            .unwrap(),
    ))
}
fn u16_at(data: &[u8], offset: usize) -> Result<u16, String> {
    Ok(u16::from_le_bytes(
        data.get(offset..offset + 2)
            .ok_or("truncated ELF")?
            .try_into()
            .unwrap(),
    ))
}
fn image(elf: &[u8]) -> Result<(Vec<u8>, u32), String> {
    if elf.get(..7) != Some(b"\x7fELF\x01\x01\x01") || u16_at(elf, 18)? != 40 {
        return Err("expected little-endian ARM ELF32".into());
    }
    let entry = u32_at(elf, 24)?;
    if !(FLASH..FLASH + 4096).contains(&entry) || entry & 1 == 0 {
        return Err("missing Thumb flash bootstrap".into());
    }
    let phoff = u32_at(elf, 28)? as usize;
    let size = usize::from(u16_at(elf, 42)?);
    let count = usize::from(u16_at(elf, 44)?);
    if size < 32 {
        return Err("invalid ELF program header".into());
    }
    let mut output = vec![0xff; 4096];
    let mut used = vec![false; 4096];
    let mut ram_high = RAM;
    for i in 0..count {
        let p = phoff
            .checked_add(i.checked_mul(size).ok_or("ELF overflow")?)
            .ok_or("ELF overflow")?;
        if u32_at(elf, p)? != 1 {
            continue;
        }
        let offset = u32_at(elf, p + 4)? as usize;
        let vaddr = u32_at(elf, p + 8)?;
        let laddr = u32_at(elf, p + 12)?;
        let filesz = u32_at(elf, p + 16)?;
        let memsz = u32_at(elf, p + 20)?;
        let vend = vaddr.checked_add(memsz).ok_or("address overflow")?;
        if filesz > memsz {
            return Err("ELF filesz exceeds memsz".into());
        }
        if (RAM..RAM_END).contains(&vaddr) {
            if vend > RAM_END - 131072 {
                return Err("runtime overlaps reserved core0 stack".into());
            }
            ram_high = ram_high.max(vend);
        } else if !(vaddr >= FLASH && vend <= FLASH + 4096) {
            return Err("runtime section is not in SRAM".into());
        }
        if filesz == 0 {
            continue;
        }
        let end = laddr.checked_add(filesz).ok_or("load address overflow")?;
        if laddr < FLASH || end > SETTINGS {
            return Err("load segment overlaps settings or is outside flash".into());
        }
        let start = (laddr - FLASH) as usize;
        let last = (end - FLASH) as usize;
        if output.len() < last {
            output.resize(last, 0xff);
            used.resize(last, false);
        }
        if used[start..last].iter().any(|v| *v) {
            return Err("overlapping load segments".into());
        }
        let source = elf
            .get(offset..offset.checked_add(filesz as usize).ok_or("file overflow")?)
            .ok_or("truncated load segment")?;
        output[start..last].copy_from_slice(source);
        used[start..last].fill(true);
    }
    let block = [0xffffded3_u32, 0x10210142, 0x000001ff, 0, 0xab123579];
    let bytes: Vec<_> = block.into_iter().flat_map(u32::to_le_bytes).collect();
    if !output[..4096].windows(bytes.len()).any(|w| w == bytes) {
        return Err("missing RP2350 secure ImageDef".into());
    }
    if output.len() <= 4096 || ram_high == RAM {
        return Err("missing RAM runtime".into());
    }
    Ok((output, ram_high - RAM + 131072))
}
fn uf2(image: &[u8]) -> Result<Vec<u8>, String> {
    if image.is_empty() || image.len() > (SETTINGS - FLASH) as usize {
        return Err("invalid image size".into());
    }
    let count = image.len().div_ceil(256);
    let mut result = vec![0u8; count * 512];
    for (index, chunk) in image.chunks(256).enumerate() {
        let block = &mut result[index * 512..(index + 1) * 512];
        let words = [
            0x0a324655,
            0x9e5d5157,
            0x2000,
            FLASH + index as u32 * 256,
            256,
            index as u32,
            count as u32,
            0xe48bff59,
        ];
        for (i, word) in words.into_iter().enumerate() {
            block[i * 4..i * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        block[32..288].fill(0xff);
        block[32..32 + chunk.len()].copy_from_slice(chunk);
        block[508..].copy_from_slice(&0x0ab16f30u32.to_le_bytes());
    }
    Ok(result)
}
fn run() -> Result<(), String> {
    let args: Vec<_> = env::args().collect();
    if args.len() != 4 || args[1] != "package" {
        return Err("usage: cargo xtask package <firmware.elf> <output.uf2>".into());
    }
    let elf = fs::read(&args[2]).map_err(|e| e.to_string())?;
    let (binary, ram) = image(&elf)?;
    let output = uf2(&binary)?;
    let path = Path::new(&args[3]);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    fs::write(path, output).map_err(|e| e.to_string())?;
    fs::write(path.with_extension("bin"), &binary).map_err(|e| e.to_string())?;
    println!(
        "flash={} / {} bytes; SRAM reservation={} / {} bytes (includes 128 KiB core0 stack)",
        binary.len(),
        SETTINGS - FLASH,
        ram,
        RAM_END - RAM
    );
    println!(
        "Created {}. Hardware acceptance is still required.",
        path.display()
    );
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn uf2_never_touches_settings() {
        assert!(uf2(&vec![0; (SETTINGS - FLASH) as usize + 1]).is_err());
        let output = uf2(&vec![42; 513]).unwrap();
        assert_eq!(output.len(), 1536);
        for (i, b) in output.chunks_exact(512).enumerate() {
            assert_eq!(u32_at(b, 12).unwrap(), FLASH + i as u32 * 256);
            assert_eq!(u32_at(b, 28).unwrap(), 0xe48bff59);
            assert_eq!(u32_at(b, 508).unwrap(), 0x0ab16f30);
        }
    }
    #[test]
    fn malformed_elf_is_rejected() {
        for len in 0..100 {
            assert!(image(&vec![0; len]).is_err());
        }
    }
}
