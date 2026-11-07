"""Build the RGB565 theme from the attributed official character artwork."""
from pathlib import Path
import math
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SCALE = 4
SIZE = 128
BG = (20, 17, 34)
PANEL = (32, 28, 48)
GOLD = (227, 199, 140)
INK = (238, 233, 243)
MUTED = (155, 149, 177)
MINT = (180, 217, 184)
FONT = 'C:/Windows/Fonts/msyh.ttc'
REACTIONS = ('陪你追光', '主人早安', '出发啦', '歇一会儿', '听你指挥',
             '跟着光走', '往左看', '往右看', '抬头啦', '低头啦', '一起转呀',
             '有点困了', '好亮呀', '找到光啦', '快充电呀', '没力气啦',
             '精神满满', '左到头啦', '右到头啦', '上到头啦', '下到头啦',
             '帮我看看', '电压未测')


def packed(values, bits):
    per_byte = 8 // bits
    return [sum(v << (bits * j) for j, v in enumerate(values[i:i + per_byte]))
            for i in range(0, len(values), per_byte)]


def array(name, values, bits):
    ctype, width = ('uint16_t', 4) if bits == 16 else ('uint8_t', 2)
    lines = [f'static const {ctype} {name}[{len(values)}] = {{']
    for offset in range(0, len(values), 16):
        lines.append(' ' + ','.join(f'0x{v:0{width}X}u' for v in values[offset:offset + 16]) + ',')
    return lines + ['};']


def build_portrait(artwork):
    crop = artwork.crop((670, 146, 956, 510)).resize(
        (57 * SCALE, 73 * SCALE), Image.Resampling.LANCZOS)
    sprite = crop.crop((0, 0, 57 * SCALE, 58 * SCALE))
    # Expression patches preserve the hair and face outside the eyes.
    expressions = []
    for expression in ('closed', 'happy', 'tired'):
        face = sprite.copy()
        draw = ImageDraw.Draw(face)
        for points in (((21.5, 14.7), (24.7, 15.4), (25.0, 18.3), (22.5, 18.5)),
                       ((27.5, 13.9), (30.5, 13.7), (30.8, 17.5), (27.7, 17.8))):
            polygon = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
            draw.polygon(polygon, fill=(255, 229, 205, 255))
        for x, y in ((22.0, 16.7), (27.9, 15.7)):
            if expression == 'happy':
                points = ((x, y + .3), (x + 1.2, y - .5), (x + 2.5, y + .1))
            elif expression == 'tired':
                points = ((x, y - .5), (x + 2.5, y), (x + 1.9, y + .6))
            else:
                points = ((x, y), (x + 1.3, y + .5), (x + 2.5, y - .1))
            draw.line([(round(a * SCALE), round(b * SCALE)) for a, b in points],
                      fill=(87, 53, 78, 255), width=2)
        expressions.append(face.resize((57, 58), Image.Resampling.LANCZOS).crop((21, 13, 32, 20)))
    sprite = sprite.resize((57, 58), Image.Resampling.LANCZOS)
    pixels = list(sprite.get_flattened_data())
    colors = [rgb565(r, g, b) for r, g, b, a in pixels]
    alpha = [round(a / 17 * min(1.0, max(0, (58 - i // 57) / 11)))
             for i, (r, g, b, a) in enumerate(pixels)]
    lines = ['#define PORTRAIT_W 57', '#define PORTRAIT_H 58',
             '#define EXPRESSION_X 21', '#define EXPRESSION_Y 13',
             '#define EXPRESSION_W 11', '#define EXPRESSION_H 7',
             '#define REACTION_TEXT_W 48', '#define REACTION_TEXT_H 11']
    lines += array('portrait_colors', colors, 16)
    lines += array('portrait_alpha', packed(alpha, 4), 8)
    expression_colors = [rgb565(r, g, b) for patch in expressions
                         for r, g, b, a in patch.get_flattened_data()]
    lines += array('portrait_expressions', expression_colors, 16)
    labels = []
    for caption in REACTIONS:
        mask = Image.new('L', (48 * SCALE, 11 * SCALE), 0)
        draw = ImageDraw.Draw(mask)
        label_font = font(10)
        x = (48 * SCALE - draw.textlength(caption, font=label_font)) / 2
        draw.text((x, -2 * SCALE), caption, font=label_font, fill=255)
        mask = mask.resize((48, 11), Image.Resampling.LANCZOS)
        labels += packed([round(v * 3 / 255) for v in mask.get_flattened_data()], 2)
    lines += array('reaction_text', labels, 8)
    return lines


def font(size):
    return ImageFont.truetype(FONT, round(size * SCALE))


def box(draw, xy, **kwargs):
    draw.rounded_rectangle(tuple(round(v * SCALE) for v in xy),
                           radius=kwargs.pop('radius', 0) * SCALE, **kwargs)


def text(draw, xy, value, color, size=12):
    draw.text((xy[0] * SCALE, (xy[1] - 2) * SCALE), value, font=font(size), fill=color)


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    (ROOT / 'evidence').mkdir(exist_ok=True)
    im = Image.new('RGB', (SIZE * SCALE, SIZE * SCALE), BG)
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            glow = max(0.0, 1.0 - math.hypot(x / SCALE - 24, y / SCALE - 53) / 65)
            px[x, y] = (round(19 + 13 * glow), round(17 + 10 * glow), round(32 + 19 * glow))
    d = ImageDraw.Draw(im)
    # A quiet moon and orbital lines behind the character.
    box(d, (7, 24, 55, 72), radius=24, fill=(46, 40, 63), outline=(77, 65, 88), width=SCALE)
    box(d, (13, 30, 49, 66), radius=18, outline=(67, 57, 78), width=SCALE)
    for x, y in ((7, 30), (48, 27), (5, 72), (52, 67), (11, 78)):
        box(d, (x, y, x + .7, y + .7), fill=GOLD)
    artwork = Image.open(ROOT / 'assets/murasame_official.png').convert('RGBA')
    # The portrait is a separate alpha sprite so animation never moves the labels.
    portrait_lines = build_portrait(artwork)
    # Fade the lower clothes so the character's name has a calm base.
    veil = Image.new('RGBA', im.size, (0, 0, 0, 0))
    vd = ImageDraw.Draw(veil)
    for y in range(76 * SCALE, 96 * SCALE):
        alpha = round(245 * min(1.0, (y / SCALE - 76) / 10))
        vd.line((0, y, 60 * SCALE, y), fill=(*BG, alpha))
    im = Image.alpha_composite(im.convert('RGBA'), veil).convert('RGB')
    d = ImageDraw.Draw(im)
    text(d, (5, 80), '女仆詹皓酱', GOLD, 10)
    d.line((8 * SCALE, 94 * SCALE, 53 * SCALE, 94 * SCALE), fill=(77, 64, 79), width=SCALE)
    # Four independent, consistently aligned sensor cells.
    for label, y in zip('左右上下', (24, 42, 60, 78)):
        box(d, (62, y, 124, y + 16), radius=3, fill=PANEL, outline=(51, 43, 66), width=SCALE)
        text(d, (66, y + 1), label, MINT)
    text(d, (5, 4), '双轴追光', GOLD)
    d.line((5 * SCALE, 20 * SCALE, 123 * SCALE, 20 * SCALE), fill=(85, 67, 85), width=SCALE)
    d.line((5 * SCALE, 20 * SCALE, 34 * SCALE, 20 * SCALE), fill=GOLD, width=SCALE)
    # Position and battery have a separate, solid background for cheap updates.
    box(d, (3, 97, 124, 125), radius=4, fill=PANEL, outline=(64, 51, 75), width=SCALE)
    text(d, (7, 98), '左右', MUTED, 11)
    text(d, (69, 98), '上下', MUTED, 11)
    text(d, (7, 112), '电池', MUTED, 11)
    im = im.resize((128, 128), Image.Resampling.LANCZOS)
    values = [rgb565(*v) for v in im.get_flattened_data()]
    raw = struct.pack('<16384H', *values)
    (ROOT / 'assets/ui_background.rgb565').write_bytes(raw)
    # Preview the same quantized colors stored in Flash.
    quantized = Image.new('RGB', (128, 128))
    quantized.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in values])
    quantized.save(ROOT / 'evidence/ui-background.png')
    lines = ['#ifndef UI_ASSETS_H', '#define UI_ASSETS_H', '#include <stdint.h>',
             '/* Derived from the attributed official artwork; see assets/SOURCE.json. */',
             'static const uint16_t ui_background[128u * 128u] = {']
    for offset in range(0, len(values), 16):
        lines.append(' ' + ','.join(f'0x{v:04X}u' for v in values[offset:offset + 16]) + ',')
    lines += ['};'] + portrait_lines + ['#endif']
    (ROOT / 'src/ui_assets.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('UI background:', len(raw), 'bytes; character: official Murasame upper-body crop')


if __name__ == '__main__':
    main()
