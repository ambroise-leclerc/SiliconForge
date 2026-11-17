"""Offline tests of the ATDF converter, tools/atdf2json (standard library only).

The command line of the application is tested by McuCliTests.cmake, without Python."""
import copy
import runpy
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
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

    def test_cpu_clock_output_is_clko_not_clk_zero(self):
        root = ET.fromstring(ATDF)
        module = ET.SubElement(root.find('./devices/device/peripherals'), 'module', name='CPU')
        instance = ET.SubElement(module, 'instance', name='CPU')
        signals = ET.SubElement(instance, 'signals')
        ET.SubElement(signals, 'signal', group='CLK', index='0', pad='PD1')
        ET.SubElement(signals, 'signal', group='CLK', index='1', pad='PD0')
        dip = CONVERT(root, 'DIP2')
        self.assertEqual(dip['package']['pins'][0]['functions'], ['CLKO', 'TXD'])
        other = CONVERT(root, 'OTHER')
        self.assertEqual(other['package']['pins'][0]['functions'], ['CLK1'])

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

    def test_missing_required_attributes_are_reported(self):
        for old, new, message in [('<register name="BAUD" offset="4"', '<register name="BAUD"', "'offset'"),
                                  ('<pin position="1" pad="PD1"/>', '<pin position="1"/>', "'pad'")]:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                CONVERT(ET.fromstring(ATDF.replace(old, new, 1)), 'DIP2')

    def test_bits_split_across_modules_are_merged(self):
        root = ET.fromstring(ATDF)
        module = ET.SubElement(root.find('./devices/device/peripherals'), 'module', name='EXTRA')
        ET.SubElement(ET.SubElement(module, 'instance', name='EXTRA'), 'register-group',
                      {'name-in-module': 'EXTRA', 'offset': '0x20', 'address-space': 'data'})
        definition = ET.SubElement(ET.SubElement(root.find('modules'), 'module', name='EXTRA'), 'register-group', name='EXTRA')
        register = ET.SubElement(definition, 'register', name='BAUD', offset='4', size='2')
        ET.SubElement(register, 'bitfield', name='OTHER', mask='0x1000')
        d = CONVERT(root, 'DIP2')
        self.assertEqual(d['registers']['BAUDH']['bits'], {'VALUE': 15, 'OTHER': 16})
        self.assertEqual(d['registers']['BAUDH']['reset'], 18)

    def test_every_timer_compare_output_is_numbered(self):
        root = ET.fromstring(ATDF)
        module = ET.SubElement(root.find('./devices/device/peripherals'), 'module', name='TC16')
        signals = ET.SubElement(ET.SubElement(module, 'instance', name='TC3'), 'signals')
        ET.SubElement(signals, 'signal', group='OCC', pad='PD1')
        self.assertEqual(CONVERT(root, 'DIP2')['package']['pins'][0]['functions'], ['OC3C', 'TXD'])

    def test_conflicting_register_definitions_are_rejected(self):
        root = ET.fromstring(ATDF)
        instance = root.find('./devices/device/peripherals/module/instance')
        ET.SubElement(instance, 'register-group', {'name-in-module': 'USART', 'offset': '0x40', 'address-space': 'data'})
        with self.assertRaisesRegex(ValueError, 'several addresses'):
            CONVERT(root, 'DIP2')

    def test_bad_package_is_explicit(self):
        with self.assertRaisesRegex(ValueError, 'Unknown package'):
            CONVERT(ET.fromstring(ATDF), 'missing')


if __name__ == '__main__':
    unittest.main()
