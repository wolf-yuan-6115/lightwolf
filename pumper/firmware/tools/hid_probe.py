#!/usr/bin/env python3
"""Read Pumper status/test diagnostics, or explicitly request restart/BOOTSEL."""
import argparse
import ctypes as c
import ctypes.util
import json
import struct
import sys


class NativeHid:
    """Fallback for systems with a native hidapi library but no Python package."""
    def __init__(self, serial):
        library = next((p for name in ('hidapi-hidraw', 'hidapi', 'hidapi-libusb')
                        if (p := ctypes.util.find_library(name))), None)
        if library is None:
            raise RuntimeError('Install the Python package: python -m pip install hidapi')
        self.lib = lib = c.CDLL(library)
        lib.hid_open.argtypes = [c.c_ushort, c.c_ushort, c.c_wchar_p]
        lib.hid_open.restype = c.c_void_p
        lib.hid_write.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t]
        lib.hid_read_timeout.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_int]
        lib.hid_get_feature_report.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t]
        lib.hid_error.argtypes = [c.c_void_p]
        lib.hid_error.restype = c.c_wchar_p
        lib.hid_close.argtypes = [c.c_void_p]
        if lib.hid_init() != 0:
            raise RuntimeError('Unable to initialize hidapi')
        self.device = lib.hid_open(0x2e8a, 0xf10a, serial)
        if not self.device:
            lib.hid_exit()
            raise RuntimeError('Pumper is inaccessible. If physical BOOTSEL is required, stop and wait for the owner.')

    def write(self, data):
        result = self.lib.hid_write(self.device, bytes(data), len(data))
        if result < 0:
            raise RuntimeError(str(self.lib.hid_error(self.device)))
        return result

    def read(self, length, timeout_ms):
        buffer = c.create_string_buffer(length)
        result = self.lib.hid_read_timeout(self.device, buffer, length, timeout_ms)
        if result < 0:
            raise RuntimeError(str(self.lib.hid_error(self.device)))
        return buffer.raw[:result]

    def get_feature_report(self, report_id, length):
        buffer = c.create_string_buffer(length)
        buffer[0] = bytes([report_id])
        result = self.lib.hid_get_feature_report(self.device, buffer, length)
        if result < 0:
            raise RuntimeError(str(self.lib.hid_error(self.device)))
        return buffer.raw[:result]

    def close(self):
        self.lib.hid_close(self.device)
        self.lib.hid_exit()


def open_device(serial):
    try:
        import hid  # cython-hidapi: pip install hidapi; supports Windows/macOS/Linux
    except ImportError:
        return NativeHid(serial)
    if not hasattr(hid, 'device'):
        raise RuntimeError('Install the hidapi Python package, which provides hid.device')
    device = hid.device()
    try:
        device.open(0x2e8a, 0xf10a, serial)
    except Exception:
        device.close()
        raise
    return device


def unnumbered(data):
    data = bytes(data)
    return data[1:] if len(data) == 65 and data[0] == 0 else data


def diagnostics(data):
    report = unnumbered(data)
    if len(report) != 64 or report[:4] != b'PDBG' or report[5:8] != b'\x00\x40\x00':
        raise RuntimeError('No supported test-build Feature report')
    if report[4] == 1:
        names = ('prototype_flags', 'clock_hz', 'max_block_us', 'buffered_frames', 'underrun_frames',
                 'usb_packets', 'usb_resets', 'backpressure_polls', 'requested_generation', 'applied_generation')
        return dict(zip(names, struct.unpack_from('<10I', report, 16)))
    if report[4] in (2, 3):
        names = ('build_flags', 'clock_hz', 'max_block_us', 'buffered_frames', 'underrun_frames',
                 'max_spectrum_slice_us', 'spectrum_fifo_high_water', 'spectrum_capture_drops',
                 'spectrum_frames', 'spectrum_analysis_drops', 'core0_stack_free', 'core1_stack_free')
        result = dict(zip(names, struct.unpack_from('<12I', report, 16)))
        result['build_id'] = report[8:16].hex()
        if report[4] == 3:
            result.pop('build_flags')
            result['last_flash_result'] = struct.unpack_from('<i', report, 16)[0]
        return result
    if report[4] == 4:
        names = ('spectrum_capture_drops', 'spectrum_analysis_drops', 'core0_stack_free',
                 'core1_stack_free', 'ownership_faults', 'usb_resets', 'underrun_frames',
                 'clock_hz', 'last_flash_result', 'spectrum_frames')
        result = dict(zip(names, struct.unpack_from('<8IiI', report, 24)))
        result['max_block_us'], result['max_spectrum_slice_us'], result['spectrum_fifo_high_water'] = struct.unpack_from('<IHH', report, 16)
        result['build_id'] = report[8:16].hex()
        return result
    raise RuntimeError('Unsupported diagnostic schema')


def transact(device, action):
    if action == 'diagnostics':
        return diagnostics(device.get_feature_report(0, 65))
    opcode = {'status': 2, 'restart': 0x40, 'bootsel': 0x41}[action]
    request = bytearray(65)
    request[1:9] = struct.pack('<2sBBHBB', b'PE', 1, opcode, 0x5752, 0, 0)
    if device.write(request) != 65:
        raise RuntimeError('Incomplete HID output report')
    for _ in range(20):
        b = unnumbered(device.read(65, 100))
        if len(b) != 64 or b[:4] != bytes([80, 69, 1, opcode | 0x80]) or b[4:6] != b'RW':
            continue
        if b[7]:
            raise RuntimeError(f'Command failed with status {b[7]}')
        if action != 'status':
            if b[6] != 0:
                raise RuntimeError('Unexpected reset acknowledgement payload')
            return f'{action} acknowledged'
        if b[6] != 48:
            raise RuntimeError('Unexpected status layout')
        return {'firmware': f'{b[8]}.{b[9]}', 'streaming': bool(b[11] & 1),
                'rate': struct.unpack_from('<I', b, 12)[0], 'bits': b[52],
                'underruns': struct.unpack_from('<I', b, 28)[0],
                'clock_hz': struct.unpack_from('<I', b, 40)[0],
                'max_dsp_us': struct.unpack_from('<I', b, 44)[0]}
    raise RuntimeError('No acknowledgement. Do not retry resets blindly; stop if physical BOOTSEL is required.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('status', 'diagnostics', 'restart', 'bootsel'))
    parser.add_argument('--serial', help='select one DAC when multiple are attached')
    args = parser.parse_args()
    device = open_device(args.serial)
    try:
        result = transact(device, args.action)
        print(json.dumps(result, indent=2) if isinstance(result, dict) else result)
    finally:
        device.close()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(error, file=sys.stderr)
        sys.exit(1)
