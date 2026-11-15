import struct
import unittest
import hid_probe


class HidProbeTest(unittest.TestCase):
    def test_feature_schema_and_optional_zero_report_id(self):
        report = bytearray(64)
        report[:8] = b'PDBG\x01\x00\x40\x00'
        struct.pack_into('<10I', report, 16, 1, 180000000, 12, 200, 0, 500, 2, 0, 4, 4)
        for data in (report, b'\x00' + report):
            self.assertEqual(hid_probe.diagnostics(data)['clock_hz'], 180000000)
        report[4] = 5
        with self.assertRaises(RuntimeError):
            hid_probe.diagnostics(report)

    def test_schema_four_keeps_signed_flash_errors_and_ownership_counters(self):
        report = bytearray(64)
        report[:8] = b'PDBG\x04\x00\x40\x00'
        report[8:16] = bytes(range(8))
        struct.pack_into('<IHH', report, 16, 1234, 49, 16000)
        struct.pack_into('<8IiI', report, 24, 1, 2, 36000, 23000, 0, 3, 4, 180000000, -101, 45)
        decoded = hid_probe.diagnostics(b'\x00' + report)
        self.assertEqual(decoded['last_flash_result'], -101)
        self.assertEqual(decoded['max_spectrum_slice_us'], 49)
        self.assertEqual(decoded['ownership_faults'], 0)
        self.assertEqual(decoded['usb_resets'], 3)
        self.assertEqual(decoded['build_id'], '0001020304050607')

    def test_reset_ignores_unrelated_reports_and_requires_matching_ack(self):
        class Device:
            def __init__(self):
                self.reports = [bytes(64), b'PE\x01\xc0RW\x00\x00' + bytes(56)]

            def write(self, data):
                self.sent = bytes(data)
                return len(data)

            def read(self, length, timeout):
                return self.reports.pop(0)

        device = Device()
        self.assertEqual(hid_probe.transact(device, 'restart'), 'restart acknowledged')
        self.assertEqual(device.sent[:9], b'\x00PE\x01\x40RW\x00\x00')
        self.assertEqual(len(device.sent), 65)

    def test_short_write_does_not_wait_or_retry_reset(self):
        class Device:
            def write(self, data):
                return 8
        with self.assertRaises(RuntimeError):
            hid_probe.transact(Device(), 'bootsel')


if __name__ == '__main__':
    unittest.main()
