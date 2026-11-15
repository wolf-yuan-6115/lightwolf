use std::{fs, path::Path};
fn fingerprint(path: &Path, root: &Path, hash: &mut u64) {
    if path.is_dir() {
        let mut children: Vec<_> = fs::read_dir(path)
            .unwrap()
            .map(|e| e.unwrap().path())
            .collect();
        children.sort();
        for child in children {
            fingerprint(&child, root, hash);
        }
    } else {
        let name = path
            .strip_prefix(root)
            .unwrap()
            .to_string_lossy()
            .replace('\\', "/");
        for byte in name.bytes().chain(fs::read(path).unwrap()) {
            *hash = (*hash ^ u64::from(byte)).wrapping_mul(0x100000001b3);
        }
    }
}
fn main() {
    let manifest = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = &manifest;
    let mut hash = 0xcbf29ce484222325u64;
    for file in [
        "core/src",
        "src",
        "ram.x",
        "memory.x",
        "build.rs",
        "Cargo.toml",
        "Cargo.lock",
        "core/Cargo.toml",
        "rust-toolchain.toml",
        ".cargo/config.toml",
        "tools/build_firmware.py",
        "vendor/embassy-rp/src",
        "vendor/embassy-usb/src",
    ] {
        let p = root.join(file);
        println!("cargo:rerun-if-changed={}", p.display());
        fingerprint(&p, root, &mut hash);
    }
    let out = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    fs::write(
        out.join("build_id.rs"),
        format!("pub const BUILD_ID:[u8;8]={:?};", hash.to_le_bytes()),
    )
    .unwrap();

    println!("cargo:rerun-if-changed=memory.x");
    println!("cargo:rerun-if-changed=ram.x");
    println!(
        "cargo:rustc-link-search={}",
        std::env::var("CARGO_MANIFEST_DIR").unwrap()
    );
    println!("cargo:rustc-link-arg=-Tram.x");
    println!("cargo:rustc-link-arg=--nmagic");
}
