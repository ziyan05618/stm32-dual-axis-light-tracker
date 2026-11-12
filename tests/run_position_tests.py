"""Run position-journal power-cut tests and actual main.c gestures on ARM."""
from pathlib import Path
import json
import os
import shutil
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0

ROOT = Path(__file__).resolve().parents[1]
(ROOT / 'build').mkdir(exist_ok=True)
(ROOT / 'evidence').mkdir(exist_ok=True)
tool_dir = Path(os.environ.get('ARM_GNU_BIN', ROOT / 'tools/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin'))
GCC = str(tool_dir / 'arm-none-eabi-gcc.exe') if (tool_dir / 'arm-none-eabi-gcc.exe').is_file() else shutil.which('arm-none-eabi-gcc')
assert GCC, 'Set ARM_GNU_BIN or add arm-none-eabi-gcc to PATH'


def run(name, sources):
    output = ROOT / 'build' / (name + '.elf')
    arguments = [GCC, '-mcpu=cortex-m3', '-mthumb', '-Os', '-Wall', '-Wextra', '-Werror',
                 '-std=c11', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
                 '-nostdlib', '-Isrc', *sources, '-Ttests/selftest.ld', '-Wl,--gc-sections', '-lgcc', '-o', str(output)]
    subprocess.run(arguments, cwd=ROOT, check=True)
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    u.mem_map(0x08000000, 0x10000)
    u.mem_map(0x20000000, 0x5000)
    u.mem_map(0x40010000, 0x2000)
    with output.open('rb') as stream:
        elf = ELFFile(stream)
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS' and section['sh_size']:
                u.mem_write(section['sh_addr'], section.data())
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    u.reg_write(UC_ARM_REG_SP, 0x20004ff0)
    u.reg_write(UC_ARM_REG_LR, 0x0800fff1)
    u.emu_start(symbols['selftest'] | 1, 0x0800fff0, timeout=30000000, count=200000000)
    code = u.reg_read(UC_ARM_REG_R0)
    checks = struct.unpack('<I', u.mem_read(symbols['checks'], 4))[0]
    assert u.reg_read(UC_ARM_REG_PC) == 0x0800fff0, (name, 'did not finish')
    assert code == 0, (name, 'failed C line', code, 'checks', checks)
    return {'suite': name, 'checks': checks, 'result': code}


report = [run('position_store_selftest', ['tests/position_store_selftest.c', 'src/position_store.c']),
          run('main_position_selftest', ['tests/main_position_selftest.c', 'src/control.c', 'src/position_store.c'])]
(ROOT / 'evidence/position-selftest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
