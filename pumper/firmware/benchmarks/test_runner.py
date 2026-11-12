import struct
import tempfile
import unittest
from pathlib import Path
from device_runner import validate_image


def block(address=0x10000000, family=0xE48BFF59, flags=0x2000):
    b = bytearray(512)
    struct.pack_into('<8I', b, 0, 0x0A324655, 0x9E5D5157, flags, address, 256, 0, 1, family)
    struct.pack_into('<I', b, 508, 0x0AB16F30)
    return b


class ImageValidation(unittest.TestCase):
    def validate(self, data):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/'test.uf2'
            p.write_bytes(data)
            return validate_image(p)

    def test_application_boundaries(self):
        self.validate(block(0x10000000))
        self.validate(block(0x101FDF00))
        for address in [0, 0x0FFFFFFF, 0x101FDF01, 0x101FE000, 0x10200000]:
            with self.assertRaises(ValueError):
                self.validate(block(address))

    def test_chip_family_and_headers(self):
        for b in [block(family=0xE48BFF56), block(flags=1), bytearray(512), b'']:
            with self.assertRaises(ValueError):
                self.validate(b)

    def test_bootrom_ignored_block(self):
        ignored = block(0x10FFFF00, 0xE48BFF57, 0xA000)
        struct.pack_into('<I', ignored, 288, 0x9957E304)
        self.validate(ignored + block())
        ignored[288] ^= 1
        with self.assertRaises(ValueError):
            self.validate(ignored + block())

if __name__ == '__main__':
    unittest.main()
