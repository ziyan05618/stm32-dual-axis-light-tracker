from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
chars=''.join(dict.fromkeys('双轴追光模式自动手安全运行左右上下电池位置信度按键长启停请亮故障低量示毫伏估先居中再动机从启止传感器要调试准备就绪校准完方向反校电亮短换实时更新'))
chars=''.join(dict.fromkeys(chars+'回存错待'))
font_path='C:/Windows/Fonts/msyh.ttc'
font=ImageFont.truetype(font_path,12)
lines=['#ifndef FONTZH12_H','#define FONTZH12_H','#include <stdint.h>','typedef enum {']
for i,ch in enumerate(chars): lines.append(f' ZH_{ord(ch):04X} = {i}, /* {ch} */')
lines.extend([f' ZH_COUNT = {len(chars)}','} zh_glyph_t;','static const uint16_t font_zh12[ZH_COUNT][12] = {'])
for ch in chars:
 im=Image.new('L',(12,12),0); d=ImageDraw.Draw(im); d.text((0,-2),ch,font=font,fill=255)
 rows=[]
 for y in range(12):
  mask=0
  for x in range(12):
   if im.getpixel((x,y))>=80: mask|=1<<(11-x)
  rows.append(mask)
 lines.append(' { '+', '.join('0x%03X'%v for v in rows)+' }, /* '+ch+' */')
lines.append('};')
lines.append('static const uint16_t font_zh_codepoints[ZH_COUNT] = { '+', '.join('0x%04X'%ord(ch) for ch in chars)+' };')
lines.append('#endif')
Path('src/font_zh12.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
# Create a preview strip for every glyph to inspect before flashing.
preview=Image.new('RGB',(len(chars)*32,44),(8,8,30)); draw=ImageDraw.Draw(preview)
for i,ch in enumerate(chars):
 draw.text((i*32+1,0),ch,font=font,fill=(255,255,255))
 draw.text((i*32+1,18),f'{ord(ch):04X}',fill=(100,190,220))
preview.save('evidence/font_zh12_preview.png')
print('generated',len(chars),'unique glyphs')


