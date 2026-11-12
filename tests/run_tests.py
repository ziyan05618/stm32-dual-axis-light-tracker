from pathlib import Path
import os, shutil
import json, struct, subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
root=Path(__file__).resolve().parents[1]
gcc=root/'tools/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
if os.environ.get('ARM_GNU_BIN'):
 gcc=Path(os.environ['ARM_GNU_BIN'])/'arm-none-eabi-gcc.exe'
elif not gcc.is_file():
 gcc=Path(shutil.which('arm-none-eabi-gcc.exe') or str(gcc))
(root/'build').mkdir(exist_ok=True)
(root/'evidence').mkdir(exist_ok=True)
args=[str(gcc),'-mcpu=cortex-m3','-mthumb','-Os','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-nostdlib','-Isrc','tests/control_selftest.c','src/control.c','-Ttests/selftest.ld','-Wl,--gc-sections','-lgcc','-o','build/control_selftest.elf']
subprocess.run(args,cwd=root,check=True)
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
u.mem_map(0x08000000,0x10000);u.mem_map(0x20000000,0x5000)
with (root/'build/control_selftest.elf').open('rb') as stream:
 elf=ELFFile(stream)
 for sec in elf.iter_sections():
  if sec['sh_flags']&2 and sec['sh_type']!='SHT_NOBITS' and sec['sh_size']:
   u.mem_write(sec['sh_addr'],sec.data())
 symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
u.reg_write(UC_ARM_REG_SP,0x20004ff0);u.reg_write(UC_ARM_REG_LR,0x0800fff1)
u.emu_start(symbols['selftest']|1,0x0800fff0,timeout=15000000,count=10000000)
result=u.reg_read(UC_ARM_REG_R0)
checks=struct.unpack('<I',u.mem_read(symbols['checks'],4))[0]
report={'arm_control_checks':checks,'result':result,'returned':u.reg_read(UC_ARM_REG_PC)==0x0800fff0}
blob=(root/'build/tracker.bin').read_bytes();sp,reset=struct.unpack_from('<II',blob)
assert sp==0x20005000 and reset&1 and 0x08000000<=reset<0x08000000+len(blob)
assert len(blob)<=63488  # The last two 1 KiB pages belong to the position journal.
with (root/'build/tracker.elf').open('rb') as stream:
 elf=ELFFile(stream)
 for seg in elf.iter_segments():
  if seg['p_type']=='PT_LOAD' and seg['p_filesz']:
   assert 0x08000000<=seg['p_paddr']<0x0800F800
   assert seg['p_paddr']+seg['p_filesz']<=0x0800F800
   assert seg['p_paddr']+seg['p_filesz']<=0x08000000+len(blob)
report['firmware_bytes']=len(blob);report['vector_and_load_addresses_valid']=True
(root/'evidence/control-selftest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
assert report['returned'] and result==0, f'Failed C source line {result}'
