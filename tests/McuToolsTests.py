"""Offline ATDF conversion and end-to-end CLI discovery tests (standard library only)."""
import copy
import json
import runpy
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = Path(sys.argv.pop()) if len(sys.argv) > 1 else None
CONVERT = runpy.run_path(str(ROOT / 'tools/atdf2json'))['convert']
# Synthetic fixture: offset register groups, a 16-bit register, vector and signal, two packages.
ATDF = '''<avr-tools-device-file><devices><device name="TestAVR" architecture="AVR8">
<address-spaces><address-space>
<memory-segment name="FLASH" start="0" size="0x4000" pagesize="0x80"/>
<memory-segment name="IRAM" start="0x100" size="0x400"/>
<memory-segment name="MAPPED_IO" start="0x20" size="0xe0"/>
<memory-segment name="REGISTERS" start="0" size="32"/>
<memory-segment name="EEPROM" start="0" size="512"/>
</address-space></address-spaces><peripherals><module name="USART"><instance name="USART0">
<register-group name-in-module="USART" offset="0x20" address-space="data"/>
<signals><signal group="TXD" pad="PD1"/></signals></instance></module></peripherals>
<interrupts><interrupt name="RESET" index="0"/><interrupt name="USART_TX" index="20"/></interrupts>
</device></devices><modules><module name="USART"><register-group name="USART">
<register name="BAUD" offset="4" size="2" initval="0x1234"><bitfield name="VALUE" mask="0xfff"/></register>
</register-group></module></modules><pinouts><pinout name="DIP2"><pin position="1" pad="PD1"/>
<pin position="2" pad="GND"/></pinout><pinout name="OTHER"><pin position="1" pad="PD0"/>
<pin position="2" pad="GND"/></pinout></pinouts></avr-tools-device-file>'''


class AtDFTests(unittest.TestCase):
    def test_imports_offsets_split_registers_vectors_memories_and_functions(self):
        d = CONVERT(ET.fromstring(ATDF), 'DIP2')
        self.assertEqual(d['name'], 'TestAVR')
        self.assertEqual(d['memories']['flash'], {'start': 0, 'size': 16384, 'pageSize': 128})
        self.assertEqual(d['registers']['BAUDL'], {'address': 36, 'reset': 52, 'bits': {'VALUE': 255}})
        self.assertEqual(d['registers']['BAUDH'], {'address': 37, 'reset': 18, 'bits': {'VALUE': 15}})
        self.assertEqual(d['vectors']['USART_TX'], 20)
        self.assertEqual(d['package']['pins'][0]['functions'], ['TXD'])
        self.assertIsNone(d['clockHz'])  # No fabricated timing defaults.

    def test_base_only_preserves_fields_not_available_in_atdf(self):
        base = CONVERT(ET.fromstring(ATDF), 'DIP2')
        base['timing'] = {'nop': {'cycles': 9}}
        base['package']['pins'][0]['label'] = 'D1'
        base['memories']['flash']['size'] = 2
        original = copy.deepcopy(base)
        d = CONVERT(ET.fromstring(ATDF), 'DIP2', base)
        self.assertEqual(base, original)
        self.assertEqual(d['timing'], base['timing'])
        self.assertEqual(d['package']['pins'][0]['label'], 'D1')
        self.assertEqual(d['memories']['flash']['size'], 16384)
        other = CONVERT(ET.fromstring(ATDF), 'OTHER', base)
        self.assertEqual(other['package']['pins'][0]['label'], '')

    def test_bad_package_is_explicit(self):
        with self.assertRaisesRegex(ValueError, 'Unknown package'):
            CONVERT(ET.fromstring(ATDF), 'missing')


@unittest.skipIf(APP is None, 'application not built')
class CliTests(unittest.TestCase):
    def test_discovery_help_load_and_invalid_description(self):
        with tempfile.TemporaryDirectory(prefix='siliconforge-mcus-') as temp:
            directory = Path(temp)
            d = json.loads((ROOT / 'mcus/atmega328p.json').read_text())
            d['name'] = 'ATmega168'
            d['memories']['flash']['size'] = 16384
            d['memories']['sram']['size'] = 1024
            d['memories']['eeprom']['size'] = 512
            d['registers']['SPH']['reset'] = 4
            chip = directory / 'atmega168.json'
            chip.write_text(json.dumps(d))
            for args in [('--help', '--mcu-path', temp), ('--mcu-path', temp, '--help')]:
                help_result = subprocess.run([str(APP), *args], capture_output=True, text=True, check=True)
                self.assertIn('atmega328p atmega168', help_result.stdout)
                self.assertIn('--mcu-file', help_result.stdout)
            for args in [('--mcu-file', str(chip)), ('--mcu-path', temp, '--mcu', 'ATMEGA168')]:
                subprocess.run([str(APP), *args, '--headless', '--speed', 'max', '--duration', '0.1',
                                str(ROOT / 'firmware/prebuilt/blink.hex')], capture_output=True, check=True)
            d['peripherals'][6]['vector'] = 'missing'
            chip.write_text(json.dumps(d))
            result = subprocess.run([str(APP), '--mcu-file', str(chip)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('/peripherals/6/vector', result.stderr)


if __name__ == '__main__':
    unittest.main()
