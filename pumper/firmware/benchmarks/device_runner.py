#!/usr/bin/env python3
"""Silent RP2350 benchmark runner. Always restores the supplied production UF2."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import termios
import time
import tty


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def wait_for(fn, seconds=20):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        result = fn()
        if result:
            return result
        time.sleep(0.2)
    raise TimeoutError(f"Timed out waiting for {fn.__name__}")


class Hid:
    def __init__(self, serial):
        self.serial = serial
        self.rid = 0xD500
        def locate():
            for p in Path('/sys/class/hidraw').glob('*'):
                props = (p / 'device/uevent').read_text()
                if 'HID_ID=0003:00002E8A:0000F10A' in props and f'HID_UNIQ={serial}\n' in props:
                    try:
                        return os.open('/dev/' + p.name, os.O_RDWR | os.O_NONBLOCK)
                    except PermissionError:
                        pass
        self.fd = wait_for(locate)

    def close(self):
        os.close(self.fd)

    def request(self, op, payload=b''):
        # Drain unsolicited meters/spectrum before writing. Linux hidraw has a
        # small per-reader queue; a reply arriving into a full queue is dropped.
        while select.select([self.fd], [], [], 0)[0]:
            os.read(self.fd, 64)
        self.rid = (self.rid + 1) & 65535
        report = bytearray(64)
        report[:4] = bytes([80, 69, 1, op])
        struct.pack_into('<H', report, 4, self.rid)
        report[6] = len(payload)
        report[8:8+len(payload)] = payload
        assert os.write(self.fd, b'\0' + report) == 65
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if not select.select([self.fd], [], [], max(0, deadline-time.monotonic()))[0]:
                break
            r = os.read(self.fd, 64)
            if len(r) == 64 and r[:4] == bytes([80, 69, 1, op | 128]) and struct.unpack_from('<H', r, 4)[0] == self.rid:
                if r[7]:
                    raise RuntimeError(f'HID {op:#x} returned {r[7]}')
                return r[8:8+r[6]]
        raise TimeoutError(f'HID {op:#x}')

    def snapshot(self):
        return {'status': self.request(2).hex(), 'global': self.request(3).hex(),
                'bands': [self.request(4, bytes([i])).hex() for i in range(10)],
                'profiles': self.request(5).hex(), 'crossfeed': self.request(7).hex(),
                'output': self.request(8).hex()}


def boot_volume(serial):
    devices = json.loads(command('lsblk', '-J', '-o', 'NAME,SERIAL,MOUNTPOINTS'))['blockdevices']
    for d in devices:
        if d.get('serial') == serial and d.get('children'):
            part = d['children'][0]
            mounts = [x for x in part.get('mountpoints', []) if x]
            if not mounts:
                # UDisks can lag behind kernel block-device enumeration.
                subprocess.run(['udisksctl', 'mount', '-b', '/dev/' + part['name']],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return None
            p = Path(mounts[0])
            if (p/'INFO_UF2.TXT').exists() and 'Board-ID: RP2350' in (p/'INFO_UF2.TXT').read_text():
                return p
    return None


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_image(path):
    data = Path(path).read_bytes()
    if len(data) % 512:
        raise ValueError('Invalid UF2 size')
    count = 0
    for off in range(0, len(data), 512):
        h = struct.unpack_from('<8I', data, off)
        require(h[:2] == (0x0A324655, 0x9E5D5157), 'Invalid UF2 header')
        require(struct.unpack_from('<I', data, off+508)[0] == 0x0AB16F30, 'Invalid UF2 trailer')
        if h[2] == 0xA000 and h[7] == 0xE48BFF57:
            require(h[4] == 256 and struct.unpack_from('<I', data, off+288)[0] == 0x9957E304, 'Invalid ignored block')
            continue
        require(h[2] == 0x2000 and h[7] == 0xE48BFF59, 'Expected RP2350 Arm Secure image')
        require(h[4] <= 476 and 0x10000000 <= h[3] < h[3]+h[4] <= 0x101FE000, 'UF2 intersects reserved settings or lies outside application flash')
        count += 1
    require(count > 0, 'Empty application image')
    return data


def flash(serial, path):
    data = validate_image(path)
    devices = json.loads(command('lsblk', '-J', '-o', 'NAME,SERIAL'))['blockdevices']
    if not any(d.get('serial') == serial for d in devices):
        d = Hid(serial)
        d.request(0x41)
        d.close()
    volume = wait_for(lambda: boot_volume(serial))
    with (volume/'BENCH.UF2').open('wb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    time.sleep(1)
    d = Hid(serial)
    status = d.request(2)
    d.close()
    assert status[:2] == bytes([5, 1])
    print('Flashed', str(path), 'SHA256', hashlib.sha256(data).hexdigest(), flush=True)


class Cdc:
    def __init__(self, serial):
        def locate():
            for p in Path('/sys/class/tty').glob('ttyACM*'):
                props = command('udevadm', 'info', '--query=property', '--name=/dev/'+p.name)
                if f'ID_SERIAL_SHORT={serial}' in props:
                    return '/dev/'+p.name
        path = wait_for(locate)
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS))
        self.pending = b''
        time.sleep(0.2)

    def request(self, line):
        os.write(self.fd, (line+'\n').encode())
        end = time.monotonic()+5
        while time.monotonic() < end:
            if b'\n' in self.pending:
                raw, self.pending = self.pending.split(b'\n', 1)
                result = json.loads(raw)
                if 'error' in result:
                    raise RuntimeError(result)
                return result
            if select.select([self.fd], [], [], max(0, end-time.monotonic()))[0]:
                self.pending += os.read(self.fd, 4096)
        raise TimeoutError('CDC '+line)


def node(serial):
    for n in json.loads(command('pw-dump')):
        p = n.get('info', {}).get('props', {})
        if p.get('media.class') == 'Audio/Sink' and serial in p.get('node.name', ''):
            return n['id'], p['node.name']


def run_case(serial, cdc, mode, scenario, rate, bits, boost, seconds):
    node_id, sink = wait_for(lambda: node(serial))
    command('pactl', 'suspend-sink', sink, '1')
    d = Hid(serial)
    wait_for(lambda: not (d.request(2)[3] & 1))
    fmt = {16: 'S16LE', 24: 'S24LE', 32: 'S32LE'}[bits]
    props = '{ params = [ "audio.format" "%s" "audio.rate" %d "audio.allowed-rates" "[%d]" ] }' % (fmt, rate, rate)
    command('pw-cli', 'set-param', str(node_id), 'Props', props)
    assert cdc.request(f'run {mode} {scenario} 3 {boost}').get('ready')
    d.request(0x30, struct.pack('<HH', 20, 2000))
    d.request(0x34)
    command('pactl', 'suspend-sink', sink, '0')
    before = struct.unpack_from('<I', d.request(2), 20)[0]
    with open('/dev/zero', 'rb') as zero, open('/tmp/pumper-fixed-benchmark/pw-cat.log', 'w') as errors:
        stream = subprocess.Popen(['pw-cat', '--playback', '--raw', '--rate', str(rate), '--format', 's32', '--channels', '2', '--target', sink, '-'], stdin=zero, stdout=subprocess.DEVNULL, stderr=errors)
        samples = []
        error = None
        started = time.monotonic()
        try:
            for second in range(seconds):
                time.sleep(1)
                assert stream.poll() is None, Path('/tmp/pumper-fixed-benchmark/pw-cat.log').read_text()
                d.request(0x31)
                d.request(0x35)
                s = cdc.request('stats')
                if s.get('busy'):
                    continue
                samples.append(s)
                if second % 15 == 14 or second == seconds-1:
                    load = s['total_us'] * rate / max(s['frames'], 1) / 10000
                    print(f"mode={mode} scenario={scenario} {bits}/{rate} boost={boost} {second+1}s load={load:.1f}% max={s['max_us']}us underruns={s['underruns']-before}", flush=True)
            assert samples and samples[-1]['rate'] == rate and samples[-1]['bits'] == bits
            expected = 200000000 if boost and mode >= 2 and bits == 32 and rate == 96000 else 180000000
            assert samples[-1]['clock'] == expected, samples[-1]
        except (TimeoutError, RuntimeError, AssertionError) as exc:
            error = str(exc)
            print('CASE FAILED:', error, flush=True)
        finally:
            stream.terminate()
            stream.wait(timeout=5)
            command('pactl', 'suspend-sink', sink, '1')
            wait_for(lambda: not (d.request(2)[3] & 1))
            assert cdc.request('stats')['clock'] == 150000000
            d.close()
        return {'mode': mode, 'scenario': scenario, 'rate': rate, 'bits': bits,
                'boost': boost, 'seconds': seconds, 'elapsed_seconds': time.monotonic()-started,
                'error': error, 'underruns_before': before, 'samples': samples}


def restore(serial, production, snapshot, default_sink):
    flash(serial, production)
    d = Hid(serial)
    # Restore volatile selection/configuration, without issuing any save command.
    profile = bytes.fromhex(snapshot['profiles'])[1]
    if profile < 8:
        try:
            d.request(0x22, bytes([profile]))
        except RuntimeError:
            pass  # Empty selection; restore the exact live configuration below.
    d.request(0x10, bytes.fromhex(snapshot['global']))
    for band in snapshot['bands']:
        d.request(0x11, bytes.fromhex(band))
    d.request(0x12, bytes.fromhex(snapshot['crossfeed'])[:8])
    d.request(0x13, bytes.fromhex(snapshot['output'])[:8])
    after = d.snapshot()
    assert after['global'] == snapshot['global'] and after['bands'] == snapshot['bands']
    assert after['crossfeed'] == snapshot['crossfeed'] and after['output'] == snapshot['output']
    d.close()
    wait_for(lambda: node(serial))
    command('pactl', 'set-default-sink', default_sink)
    print('RESTORED production 5.1, live/saved controls, default output.', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--benchmark', required=True, type=Path)
    p.add_argument('--production', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--resume', action='store_true', help='Retain completed cases in the output file')
    a = p.parse_args()
    validate_image(a.benchmark)
    validate_image(a.production)
    require(a.benchmark.resolve() != a.production.resolve() and a.benchmark.read_bytes() != a.production.read_bytes(), 'Benchmark and production backup must be different images')
    d = Hid(a.serial)
    snapshot = d.snapshot()
    d.close()
    default = command('pactl', 'get-default-sink')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.with_suffix('.before.json').write_text(json.dumps(snapshot, indent=2))
    records = json.loads(a.output.read_text()) if a.resume and a.output.exists() else []
    image_hash = hashlib.sha256(a.benchmark.read_bytes()).hexdigest()
    def execute(case):
        keys = ['mode', 'scenario', 'rate', 'bits', 'boost', 'seconds']
        if any(not r.get('error') and tuple(r[k] for k in keys) == case for r in records):
            return
        r = run_case(a.serial, cdc, *case)
        r['benchmark_sha256'] = image_hash
        records.append(r)
        a.output.write_text(json.dumps(records, indent=2))
    try:
        flash(a.serial, a.benchmark)
        cdc = Cdc(a.serial)
        cases = [(m, sc, 96000, 32, 0, 8 if a.quick else 60) for m in [0, 2, 3] for sc in ([0] if a.quick else [0, 1, 2])]
        if not a.quick:
            cases.append((1, 0, 96000, 32, 0, 60))
        for case in cases:
            execute(case)
        if not a.quick:
            # Only state64 passes the current-preset numerical impulse-tail gate.
            # Try boost only if its 180 MHz timing misses the 70/85% targets.
            wide = [r for r in records if r['mode'] == 3 and r['samples']]
            if any(r['samples'][-1]['total_us'] * 96000 / r['samples'][-1]['frames'] / 10000 > 70 or r['samples'][-1]['peak_us']*96000/max(r['samples'][-1]['peak_frames'],1)/10000 > 85 for r in wide):
                for sc in [0, 1, 2]:
                    execute((3, sc, 96000, 32, 1, 60))
            for bits, rates in [(16, [44100,48000,88200,96000,176400,192000]), (24,[44100,48000,88200,96000]), (32,[44100,48000,88200])]:
                for rate in rates:
                    execute((3, 1, rate, bits, 1, 60 if rate==192000 else 8))
        os.close(cdc.fd)
    finally:
        restore(a.serial, a.production, snapshot, default)

if __name__ == '__main__':
    main()
