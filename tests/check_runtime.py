"""Read the running target through ST-Link without halting or moving motors."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]


def read_flash_log(path):
    content = path.read_bytes()
    encoding = 'utf-16' if content.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    return content.decode(encoding)


def main():
    firmware = (ROOT / 'build/tracker.bin').read_bytes()
    expected = hashlib.sha256(firmware).hexdigest().upper()
    assert expected == hashlib.sha256((ROOT / 'build/tracker-readback.bin').read_bytes()).hexdigest().upper()
    with (ROOT / 'build/tracker.elf').open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    fields = {'g_boot_marker': (32, 1), 'g_io_ready': (8, 1), 'g_adc_fault': (8, 1),
              'g_enabled': (8, 1), 'g_millis': (32, 1), 'g_ui_frames': (32, 1),
              'g_ui_max_ms': (32, 1), 'g_ui_last_ms': (32, 1),
              'g_ui_chunk_max_ms': (32, 1), 'g_ui_chunk_last_ms': (32, 1),
              'g_ui_portrait': (8, 9), 'g_adc_raw': (16, 5),
              'g_battery_mv': (16, 1), 'g_light': (16, 4),
              'g_position_known': (8, 1), 'g_position_dirty': (8, 1), 'g_position_fault': (8, 1),
              'g_returning': (8, 1), 'g_boot_home_pending': (8, 1)}
    arguments = [str(ROOT / 'tools/openocd/xpack-openocd-0.12.0-7/bin/openocd.exe'),
                 '-f', 'tools/stlink-target.cfg', '-c', 'init']
    for sample in range(3):
        if sample:
            arguments += ['-c', 'sleep 1200']
        for name, (width, count) in fields.items():
            arguments += ['-c', f'echo "RUNTIME{sample}_{name}=[read_memory {symbols[name]:#x} {width} {count}]"']
        arguments += ['-c', f'echo "RUNTIME{sample}_cfsr=[read_memory 0xe000ed28 32 1]"']
    arguments += ['-c', 'shutdown']
    completed = subprocess.run(arguments, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=25, text=True, encoding='utf-8', errors='replace')
    log_name = 'runtime-check-startup-home.log'
    (ROOT / 'evidence' / log_name).write_text(completed.stdout, encoding='utf-8')
    assert completed.returncode == 0, completed.stdout
    samples = []
    for sample in range(3):
        values = {}
        for name in (*fields, 'cfsr'):
            match = re.search(rf'^RUNTIME{sample}_{name}=(.+)$', completed.stdout, re.M)
            assert match, (sample, name, completed.stdout)
            values[name] = [int(value, 0) for value in match.group(1).split()]
        assert values['g_boot_marker'] == [0x49464e45]
        assert values['g_io_ready'] == [1] and values['g_adc_fault'] == [0]
        assert values['g_position_fault'] == [0]
        assert values['cfsr'] == [0]
        assert values['g_ui_chunk_max_ms'][0] < 20, values
        samples.append(values)
    for before, after in zip(samples, samples[1:]):
        assert after['g_millis'][0] > before['g_millis'][0], 'runtime reset or stopped'
        assert after['g_ui_frames'][0] > before['g_ui_frames'][0], 'UI stopped'
    flash_logs = []
    for path in sorted((ROOT / 'evidence').glob('flash-*.log'), reverse=True):
        if 'READBACK_SHA256_MATCH=' + expected in read_flash_log(path):
            flash_logs.append(path)
            break
    assert flash_logs, 'No successful flash verification for this build'
    flash_name = flash_logs[0].name
    stamp = flash_name.removeprefix('flash-').removesuffix('.log')
    backup = 'backups/before-flash-' + stamp + '-64KiB.bin'
    assert (ROOT / backup).stat().st_size == 65536
    report = {'firmware_sha256': expected, 'firmware_bytes': len(firmware), 'flash_log': flash_name,
              'runtime_log': log_name, 'backup': backup, 'readback_matches': True,
              'runtime_advances': True, 'ui_advances': True, 'adc_fault': 0,
              'target_not_halted_or_armed_by_check': True, 'samples': samples}
    (ROOT / 'evidence/release-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
