"""Package the current verified firmware, sources, artwork and evidence only."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    expected = digest(ROOT / 'build/tracker.bin')
    assert expected == digest(ROOT / 'build/tracker-readback.bin'), 'readback differs'
    report = json.loads((ROOT / 'evidence/ui-render-check.json').read_text(encoding='utf-8'))
    assert report['dirty_redraw_matches_fresh_frame']
    assert report['firmware_sha256'] == expected, 'UI verification is stale'
    assert report['reaction_states'] == 23 and report['nickname_untouched']
    for check in ('directional_transient_limits_checked', 'cross_axis_motion_at_limit_checked',
                  'held_limit_request_does_not_repeat', 'released_limit_tap_checked',
                  'independent_axis_timeout_checked', 'smooth_return_checked'):
        assert report[check], check
    release = json.loads((ROOT / 'evidence/release-verification.json').read_text(encoding='utf-8'))
    assert release['firmware_sha256'] == expected and release['readback_matches']
    assert release['runtime_advances'] and release['ui_advances'] and release['adc_fault'] == 0
    controls = json.loads((ROOT / 'evidence/control-selftest.json').read_text(encoding='utf-8'))
    assert controls['result'] == 0 and controls['firmware_bytes'] == (ROOT / 'build/tracker.bin').stat().st_size
    positions = json.loads((ROOT / 'evidence/position-selftest.json').read_text(encoding='utf-8'))
    assert len(positions) == 2 and all(suite['result'] == 0 for suite in positions)
    (ROOT / 'evidence/readback-hash.txt').write_text('READBACK_SHA256_MATCH=' + expected + chr(10), encoding='utf-8')
    paths = list((ROOT / 'src').glob('*')) + list((ROOT / 'tests').glob('*')) + list((ROOT / 'assets').glob('*'))
    paths += [ROOT / name for name in ['README.md', '交付记录.md', '互动效果说明.md', '回中与零点.md', '硬件核对.md', 'build.ps1', 'flash.ps1', 'Flash.cmd', 'Makefile', 'tools/stlink-target.cfg']]
    paths += [ROOT / 'build' / ('tracker.' + suffix) for suffix in ['bin', 'elf', 'hex', 'map']]
    paths += [ROOT / 'build/tracker-readback.bin']
    evidence = [ROOT / 'evidence' / name for name in ['ui-preview.png', 'ui-preview-6x.png', 'ui-preview-animation.gif', 'ui-idle-animation.gif', 'ui-boundary-smooth-animation.gif', 'ui-states-contact.png', 'ui-reactions-contact.png', 'ui-home-states.png', 'ui-background.png', 'ui-render-check.json', 'control-selftest.json', 'position-selftest.json', release['flash_log'], release['runtime_log'], 'release-verification.json', 'readback-hash.txt']]
    assert all(p.is_file() for p in evidence), 'required evidence is missing'
    flash_bytes = (ROOT / 'evidence' / release['flash_log']).read_bytes()
    flash_encoding = 'utf-16' if flash_bytes.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    assert 'READBACK_SHA256_MATCH=' + expected in flash_bytes.decode(flash_encoding)
    paths += evidence
    paths = sorted(set(p for p in paths if p.is_file()))
    assert all(p.resolve().is_relative_to(ROOT.resolve()) for p in paths)
    assert not any('node_modules' in p.parts or '.venv' in p.parts for p in paths)
    manifest = [{'Path': p.relative_to(ROOT).as_posix(), 'Bytes': p.stat().st_size, 'SHA256': digest(p)} for p in paths]
    final_hashes = ROOT / 'evidence/final-file-hashes.json'
    final_hashes.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + chr(10), encoding='utf-8')
    paths.append(final_hashes)
    manifest.append({'Path': final_hashes.relative_to(ROOT).as_posix(), 'Bytes': final_hashes.stat().st_size, 'SHA256': digest(final_hashes)})
    manifest_file = ROOT / '文件校验.json'
    manifest_file.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + chr(10), encoding='utf-8')
    paths.append(manifest_file)
    archive = ROOT / '双轴追光_源码与已烧录固件.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for p in paths:
            bundle.write(p, 'firmware/' + p.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(archive) as bundle:
        assert bundle.testzip() is None
        assert len(bundle.namelist()) == len(paths)
        for entry in manifest:
            content = bundle.read('firmware/' + entry['Path'])
            assert len(content) == entry['Bytes'] and hashlib.sha256(content).hexdigest().upper() == entry['SHA256']
    print(json.dumps({'archive': str(archive), 'members': len(paths), 'archive_bytes': archive.stat().st_size, 'firmware_bytes': controls['firmware_bytes'], 'firmware_sha256': expected, 'all_archive_hashes_match': True}, ensure_ascii=False))


if __name__ == '__main__':
    main()
