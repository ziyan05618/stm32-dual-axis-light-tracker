"""Render the actual ARM firmware's LCD command stream, without desktop control."""
from pathlib import Path
import json, hashlib
import struct
from PIL import Image, ImageDraw, ImageFont
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]


class Screen:
    def __init__(self):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.u.mem_map(0x08000000, 0x10000)
        self.u.mem_map(0x20000000, 0x5000)
        self.u.mem_map(0x40010000, 0x1000)
        with (ROOT / 'build/tracker.elf').open('rb') as stream:
            elf = ELFFile(stream)
            for sec in elf.iter_sections():
                if sec['sh_flags'] & 2 and sec['sh_type'] != 'SHT_NOBITS' and sec['sh_size']:
                    self.u.mem_write(sec['sh_addr'], sec.data())
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.pixels = [0] * 16384
        self.windows = 0
        self.bytes = 0
        self.command = 0
        self.params = []
        self.columns = (0, 127)
        self.rows = (0, 127)
        self.cursor = 0
        self.high = None
        at = self.symbols['lcd_write8'] & ~1
        self.u.hook_add(UC_HOOK_CODE, self.write_byte, begin=at, end=at)

    def write_byte(self, u, address, size, data):
        value = u.reg_read(UC_ARM_REG_R0) & 255
        is_data = u.reg_read(UC_ARM_REG_R1)
        self.bytes += 1
        if not is_data:
            self.command = value
            self.params = []
            if value == 0x2c:
                self.cursor = 0
                self.high = None
                self.windows += 1
        elif self.command in (0x2a, 0x2b):
            self.params.append(value)
            if len(self.params) == 4:
                lo = self.params[0] * 256 + self.params[1]
                hi = self.params[2] * 256 + self.params[3]
                offset = 2 if self.command == 0x2a else 3
                lo -= offset
                hi -= offset
                assert 0 <= lo <= hi < 128, ('LCD window out of range', self.command, lo, hi)
                if self.command == 0x2a:
                    self.columns = (lo, hi)
                else:
                    self.rows = (lo, hi)
        elif self.command == 0x2c:
            if self.high is None:
                self.high = value
            else:
                width = self.columns[1] - self.columns[0] + 1
                height = self.rows[1] - self.rows[0] + 1
                assert self.cursor < width * height, 'too many pixels in LCD window'
                x = self.columns[0] + self.cursor % width
                y = self.rows[0] + self.cursor // width
                self.pixels[y * 128 + x] = self.high * 256 + value
                self.cursor += 1
                self.high = None
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        self.u.reg_write(UC_ARM_REG_SP, 0x20004ff0)
        self.u.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        for reg, arg in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.u.reg_write(reg, arg)
        self.u.emu_start(self.symbols[name] | 1, 0x0800fff0, timeout=15000000, count=20000000)
        assert self.u.reg_read(UC_ARM_REG_PC) == 0x0800fff0, 'render did not return'

    def update(self, state, now):
        self.u.mem_write(0x20001000, struct.pack('<5H2xii3BbbBBx', *state))
        self.call('ui_render', 0x20001000, now)

    def render(self, state, now):
        self.update(state, now)
        for _ in range(11):
            active = self.u.mem_read(self.symbols['g_ui_anim_active'], 1)[0]
            pending = self.u.mem_read(self.symbols['g_ui_anim_pending'], 1)[0]
            if not active and not pending:
                return
            self.call('ui_animate')
        raise AssertionError('portrait animation did not finish in bounded strips')

    def image(self):
        im = Image.new('RGB', (128, 128))
        im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in self.pixels])
        return im


REACTIONS = ('idle', 'hello', 'start', 'stop', 'manual', 'auto', 'left', 'right',
             'up', 'down', 'diagonal', 'dark', 'bright', 'aligned', 'low', 'empty',
             'recovered', 'limit_left', 'limit_right', 'limit_up', 'limit_down', 'fault', 'unknown')
BASE = (684, 918, 752, 520, 3976, 0, 0, 0, 0, 0, 0, 0, 0, 0)


def state(base=BASE, *, lights=None, battery=None, pan=None, tilt=None, automatic=None, enabled=None, fault=None,
          pan_request=None, tilt_request=None, limit_attempts=None, home_status=None):
    values = list(base)
    if lights is not None:
        values[:4] = lights
    for index, value in ((4, battery), (5, pan), (6, tilt), (7, automatic), (8, enabled), (9, fault),
                         (10, pan_request), (11, tilt_request), (12, limit_attempts), (13, home_status)):
        if value is not None:
            values[index] = value
    return tuple(values)


def portrait(screen):
    return struct.unpack('<4b5B', screen.u.mem_read(screen.symbols['g_ui_portrait'], 9))


def verify_frame(screen, value, now):
    # Preserve animation history; only the fresh renderer discards its pixel cache.
    fresh = Screen()
    fresh.u.mem_write(0x20000000, bytes(screen.u.mem_read(0x20000000, 0x5000)))
    fresh.u.mem_write(fresh.symbols['ready'], bytes([0]))
    screen.render(value, now)
    fresh.render(value, now)
    assert screen.pixels == fresh.pixels, ('dirty redraw leaves old pixels', now, portrait(screen))
    background = struct.unpack('<16384H', (ROOT / 'assets/ui_background.rgb565').read_bytes())
    for y in range(80, 97):
        assert screen.pixels[y * 128:y * 128 + 61] == list(background[y * 128:y * 128 + 61]), ('nickname overwritten', y)


def main():
    screen = Screen()
    verify_frame(screen, BASE, 0)
    initial_bytes = screen.bytes
    screen.image().save(ROOT / 'evidence/ui-preview.png')
    screen.image().resize((768, 768), Image.Resampling.NEAREST).save(ROOT / 'evidence/ui-preview-6x.png')
    run = state(enabled=1)
    auto = state(automatic=1)
    low = state(battery=3300)
    dark = state(lights=(0, 0, 0, 0))
    bright = state(lights=(950, 950, 950, 950))
    aligned = state(lights=(600, 600, 600, 600), enabled=1, automatic=1)
    cases = [
        ('待机呼吸', BASE, [(BASE, 0), (BASE, 1920)], 'idle'),
        ('开机问候', BASE, [(BASE, 0)], 'hello'),
        ('启动回应', run, [(BASE, 0), (BASE, 1920), (run, 2040)], 'start'),
        ('暂停休息', BASE, [(run, 0), (run, 1920), (BASE, 2040)], 'stop'),
        ('切换手动', BASE, [(auto, 0), (auto, 1920), (BASE, 2040)], 'manual'),
        ('切换自动', auto, [(BASE, 0), (BASE, 1920), (auto, 2040)], 'auto'),
        ('往左摆动', state(run, pan=-16), [(run, 0), (run, 1920), (state(run, pan=-16), 2040)], 'left'),
        ('往右摆动', state(run, pan=16), [(run, 0), (run, 1920), (state(run, pan=16), 2040)], 'right'),
        ('向上抬起', state(run, tilt=16), [(run, 0), (run, 1920), (state(run, tilt=16), 2040)], 'up'),
        ('向下点头', state(run, tilt=-16), [(run, 0), (run, 1920), (state(run, tilt=-16), 2040)], 'down'),
        ('双轴联动', state(run, pan=16, tilt=16), [(run, 0), (run, 1920), (state(run, pan=16, tilt=16), 2040)], 'diagonal'),
        ('暗处犯困', dark, [(dark, 0), (dark, 1920)], 'dark'),
        ('强光眯眼', bright, [(bright, 0), (bright, 1920)], 'bright'),
        ('追光对准', aligned, [(aligned, 0), (aligned, 1920)], 'aligned'),
        ('低电提醒', low, [(low, 0), (low, 1920)], 'low'),
        ('电量将尽', state(battery=3050), [(state(battery=3050), 0)], 'empty'),
        ('电压恢复', state(battery=3600), [(low, 0), (low, 1920), (state(battery=3600), 2040)], 'recovered'),
        ('左侧软限位', state(run, pan=-768), [(run, 0), (run, 1920), (state(run, pan=-768), 2040)], 'limit_left'),
        ('右侧软限位', state(run, pan=768), [(run, 0), (run, 1920), (state(run, pan=768), 2040)], 'limit_right'),
        ('上方软限位', state(run, tilt=384), [(run, 0), (run, 1920), (state(run, tilt=384), 2040)], 'limit_up'),
        ('下方软限位', state(run, tilt=-384), [(run, 0), (run, 1920), (state(run, tilt=-384), 2040)], 'limit_down'),
        ('采样故障', state(fault=1), [(state(fault=1), 0)], 'fault'),
        ('电压未测', state(battery=0), [(state(battery=0), 0), (state(battery=0), 1920)], 'unknown'),
    ]
    panels, animations, results = [], [], []
    checks = 1
    windows = screen.windows
    for title, final, sequence, expected in cases:
        device = Screen()
        for value, now in sequence:
            verify_frame(device, value, now)
            checks += 1
        actual = REACTIONS[portrait(device)[5]]
        assert actual == expected, (title, expected, actual)
        panels.append((title, device.image()))
        frames = [device.image().resize((512, 512), Image.Resampling.NEAREST)]
        for tick in (120, 240):
            verify_frame(device, final, sequence[-1][1] + tick)
            frames.append(device.image().resize((512, 512), Image.Resampling.NEAREST))
            checks += 1
        animations.extend(frames)
        results.append({'name': title, 'expected': expected, 'actual': actual})
        windows += device.windows

    device = Screen()
    device.render(low, 0)
    for mv, expected in ((3300, 'low'), (3450, 'low'), (3500, 'recovered')):
        value = state(battery=mv)
        verify_frame(device, value, 2400 + checks * 120)
        assert REACTIONS[portrait(device)[5]] == expected, (mv, portrait(device))
        checks += 1
    for mv, expected in ((3100, 'empty'), (3200, 'empty'), (3300, 'low')):
        value = state(battery=mv)
        verify_frame(device, value, 100000 + checks * 2000)
        assert REACTIONS[portrait(device)[5]] == expected
        checks += 1

    device = Screen()
    device.render(run, 0)
    device.render(run, 1920)
    moved = state(run, pan=-16)
    device.render(moved, 2040)
    assert portrait(device)[0] < 0 and REACTIONS[portrait(device)[5]] == 'left'
    device.render(moved, 2400)
    assert REACTIONS[portrait(device)[5]] == 'idle', 'motion animation did not expire'
    device.render(state(pan=80), 2520)
    assert REACTIONS[portrait(device)[5]] == 'stop', 'disabled motion was animated'
    checks += 3

    def reaction_at(device, value, now):
        device.update(value, now)
        return REACTIONS[portrait(device)[5]]

    # Reaching a boundary is a short event, with its own direction and no repeat
    # while a blocked automatic or held manual request remains unchanged.
    directions = ((-768, 0, -1, 0, 1, 'limit_left'), (768, 0, 1, 0, 2, 'limit_right'),
                  (0, 384, 0, 1, 4, 'limit_up'), (0, -384, 0, -1, 8, 'limit_down'))
    for pan, tilt, pan_request, tilt_request, bit, expected in directions:
        device = Screen()
        reaction_at(device, run, 0)
        reaction_at(device, run, 1920)
        edge = state(run, pan=pan, tilt=tilt, pan_request=pan_request, tilt_request=tilt_request)
        assert reaction_at(device, edge, 2040) == expected
        assert reaction_at(device, edge, 3100) != expected
        assert reaction_at(device, edge, 6000) != expected
        # A new press while already at the boundary must be visible again.
        released = state(edge, pan_request=0, tilt_request=0)
        reaction_at(device, released, 6060)
        assert reaction_at(device, edge, 6120) == expected
        # A one-shot tap should not disappear even if released between screen updates.
        reaction_at(device, released, 7200)
        assert reaction_at(device, state(released, limit_attempts=bit), 7260) == expected
        checks += 5

    device = Screen()
    right_edge = state(run, pan=768, pan_request=1)
    reaction_at(device, run, 0)
    reaction_at(device, run, 1920)
    assert reaction_at(device, right_edge, 2040) == 'limit_right'
    up_at_edge = state(right_edge, tilt=10, tilt_request=1)
    assert reaction_at(device, up_at_edge, 2100) == 'up', 'horizontal limit hid upward motion'
    down_at_edge = state(right_edge, tilt=-10, tilt_request=-1)
    assert reaction_at(device, down_at_edge, 2160) == 'down', 'horizontal limit hid downward motion'
    assert reaction_at(device, down_at_edge, 3300) != 'limit_right', 'stale boundary hint returned'
    upper_edge = state(run, tilt=384, tilt_request=1)
    reaction_at(device, upper_edge, 4500)
    assert reaction_at(device, state(upper_edge, pan=-10, pan_request=-1), 4560) == 'left'
    assert reaction_at(device, state(upper_edge, pan=10, pan_request=1), 4620) == 'right'
    checks += 6

    # Direction is tracked per axis: the shorter movement must not keep producing
    # a diagonal reaction merely because the other axis is still moving.
    device = Screen()
    reaction_at(device, run, 0)
    reaction_at(device, run, 1920)
    for now in range(1980, 2580, 60):
        value = state(run, pan=(now - 1920) // 60 * 4, tilt=4)
        actual = reaction_at(device, value, now)
    assert actual == 'right', ('stale axis direction', actual)
    checks += 1

    # Examine actual firmware frames throughout a realistic motion and release.
    device = Screen()
    for now in range(0, 2400, 60):
        device.update(run, now)
    poses = []
    for i, now in enumerate(range(2400, 3420, 60)):
        value = state(run, pan=min(i + 1, 9) * 4)
        device.update(value, now)
        poses.append(portrait(device)[:4])
    assert max(row[0] for row in poses) >= 12, ('motion too small', poses)
    assert abs(poses[-1][0]) < abs(poses[8][0]), ('pose did not settle', poses)
    assert max(abs(b[j] - a[j]) for a, b in zip(poses, poses[1:]) for j in range(4)) <= 18, poses
    checks += 3

    device = Screen()
    for now in range(0, 4320, 60):
        device.update(BASE, now)
    for now, eyes, mix in ((4320, 0, 15), (4440, 1, 4), (4560, 1, 15), (4680, 1, 4), (4800, 0, 15)):
        verify_frame(device, BASE, now)
        assert (portrait(device)[4], portrait(device)[8]) == (eyes, mix)
        checks += 1
    before = device.bytes
    device.render(BASE, 4920)
    unchanged_bytes = device.bytes - before
    assert unchanged_bytes < 8000, ('animation redraw budget exceeded', unchanged_bytes)
    for index, value in enumerate((state(lights=(1000,) * 4, battery=4200, pan=-768, tilt=-384),
                                   state(lights=(0,) * 4, battery=3000, fault=1),
                                   state(lights=(1, 2, 1, 2), battery=0, pan=768, tilt=384))):
        verify_frame(device, value, 6000 + index * 120)
        checks += 1
    previous = list(device.pixels)
    device.u.mem_write(0x20001200, b'X' + bytes([0]))
    device.call('display_text', 126, 126, 0x20001200, 0xffff)
    assert device.pixels == previous, 'text at screen edge wrapped'
    checks += 1

    # Newly introduced boot / HOME badges must also redraw without residue.
    home_device = Screen()
    home_panels = []
    for index, (caption, status) in enumerate((('待校准零点', 3), ('开机准备', 1), ('慢速回中', 2), ('安全待机', 0), ('保存故障', 4))):
        value = state(home_status=status, enabled=int(status == 2), pan=120 if status == 2 else 0)
        verify_frame(home_device, value, index * 120)
        home_panels.append((caption, home_device.image()))
        checks += 1
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 17)
    home_sheet = Image.new('RGB', (5 * 256, 280), '#141122')
    home_draw = ImageDraw.Draw(home_sheet)
    for index, (caption, picture) in enumerate(home_panels):
        home_draw.text((index * 256 + 8, 1), caption, font=font, fill='#e3c78c')
        home_sheet.paste(picture.resize((256, 256), Image.Resampling.NEAREST), (index * 256, 24))
    home_sheet.save(ROOT / 'evidence/ui-home-states.png')
    sheet = Image.new('RGB', (5 * 256, ((len(panels) + 4) // 5) * 280), '#141122')
    draw = ImageDraw.Draw(sheet)
    for index, (title, picture) in enumerate(panels):
        x, y = (index % 5) * 256, (index // 5) * 280
        draw.text((x + 8, y + 1), title, font=font, fill='#e3c78c')
        sheet.paste(picture.resize((256, 256), Image.Resampling.NEAREST), (x, y + 24))
    sheet.save(ROOT / 'evidence/ui-states-contact.png')
    sheet.save(ROOT / 'evidence/ui-reactions-contact.png')
    animations[0].save(ROOT / 'evidence/ui-preview-animation.gif', save_all=True,
                       append_images=animations[1:], duration=180, loop=0, disposal=2)
    idle = Screen()
    frames = []
    for now in range(0, 5760, 60):
        idle.render(BASE, now)
        frames.append(idle.image().resize((512, 512), Image.Resampling.NEAREST))
    frames[0].save(ROOT / 'evidence/ui-idle-animation.gif', save_all=True,
                   append_images=frames[1:], duration=60, loop=0, disposal=2)
    # Show the reported scenario using the actual 60ms animation interval:
    # right boundary, upward/downward movement while still at that boundary,
    # then a gentle return to the resting pose.
    device = Screen()
    resting = state(run, pan=744)
    for now in range(0, 1920, 60):
        device.update(resting, now)
    demo_frames = []
    for now in range(1920, 6240, 60):
        pan = 744 + min(4, max(0, (now - 1920) // 60)) * 6
        tilt = min(12, max(0, (now - 2400) // 60)) * 4
        if now >= 3420:
            tilt = 48 - min(12, (now - 3420) // 60) * 4
        if now >= 4500:
            pan = 768 - min(10, (now - 4500) // 60) * 4
        value = state(run, pan=pan, tilt=tilt)
        device.render(value, now)
        demo_frames.append(device.image().resize((512, 512), Image.Resampling.NEAREST))
    demo_frames[0].save(ROOT / 'evidence/ui-boundary-smooth-animation.gif', save_all=True,
                        append_images=demo_frames[1:], duration=60, loop=0, disposal=2)
    report = {'rendered_actual_arm_firmware': True, 'decoded_st7735_lcd_stream': True,
              'firmware_sha256': hashlib.sha256((ROOT / 'build/tracker.bin').read_bytes()).hexdigest().upper(),
              'viewport': [128, 128], 'state_and_clipping_checks': checks,
              'reaction_states': len(results), 'reactions': results,
              'lcd_windows_checked': windows + device.windows, 'initial_spi_bytes': initial_bytes,
              'unchanged_frame_spi_bytes': unchanged_bytes, 'dirty_redraw_matches_fresh_frame': True,
              'nickname_untouched': True, 'battery_hysteresis_checked': True,
              'motion_timeout_checked': True, 'blink_checked': True,
              'directional_transient_limits_checked': True, 'cross_axis_motion_at_limit_checked': True,
              'held_limit_request_does_not_repeat': True, 'released_limit_tap_checked': True,
              'independent_axis_timeout_checked': True, 'smooth_return_checked': True,
              'animation_interval_ms': 60}
    (ROOT / 'evidence/ui-render-check.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + chr(10), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
