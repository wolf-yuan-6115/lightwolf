#!/usr/bin/env python3
"""Build and package pinned Rust firmware with machine-independent source paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--target-dir', type=Path, default=root / 'target')
    parser.add_argument('--sysroot', type=Path, help='optional staged Rust target sysroot')
    parser.add_argument('--output', type=Path, required=True, help='UF2 output path')
    args = parser.parse_args()
    target_dir = args.target_dir.resolve()
    output = args.output.resolve()
    cargo_home = Path(os.environ.get('CARGO_HOME', Path.home() / '.cargo')).resolve()
    target = 'thumbv8m.main-none-eabihf'
    flag_key = 'CARGO_TARGET_THUMBV8M_MAIN_NONE_EABIHF_RUSTFLAGS'
    if any(os.environ.get(k) for k in ('RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS', flag_key)):
        parser.error('unset custom Rust flags for a reproducible build; use --sysroot if needed')
    flags = ['-C', 'target-cpu=cortex-m33']
    # More specific mappings come last: Cargo uses last-match remapping.
    for path, replacement in ((root, '/pumper'), (cargo_home, '/cargo'), (target_dir, '/target')):
        flags.extend(['--remap-path-prefix', f'{path}={replacement}'])
    if args.sysroot:
        sysroot = args.sysroot.resolve()
        flags.extend(['--sysroot', str(sysroot), '--remap-path-prefix', f'{sysroot}=/rust-sysroot'])
    environment = os.environ.copy()
    environment['CARGO_ENCODED_RUSTFLAGS'] = '\x1f'.join(flags)
    common = ['--manifest-path', str(root / 'Cargo.toml'), '--locked', '--target-dir', str(target_dir)]
    if args.offline:
        common.append('--offline')
    build = ['cargo', 'build', '-p', 'pumper-device', '--release', '--target', target, *common]
    if args.diagnostics:
        build.extend(['--features', 'diagnostics'])
    subprocess.run(build, cwd=root, env=environment, check=True)
    elf = target_dir / target / 'release/pumper-firmware'
    subprocess.run(['cargo', 'run', '-p', 'pumper-tools', *common, '--', 'package', str(elf), str(output)],
                   cwd=root, check=True)
    manifest = {'diagnostics': args.diagnostics, 'target': target,
                'toolchain': (root / 'rust-toolchain.toml').read_text(),
                'cargo_lock_sha256': hashlib.sha256((root / 'Cargo.lock').read_bytes()).hexdigest(),
                'artifacts': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (output, output.with_suffix('.bin'))}}
    output.with_suffix('.build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest['artifacts'], indent=2))


if __name__ == '__main__':
    main()
