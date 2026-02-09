#!/usr/bin/env python3
"""
Generate LVGL 1-bpp bitmap font from a TrueType font.
Output format matches LVGL v9 lv_font_fmt_txt format.

Usage: python gen_lvgl_font.py [--font FONT] [--size SIZE] [--threshold THR] [--name NAME] [--output FILE]
"""

from PIL import Image, ImageDraw, ImageFont
import argparse
import os

def render_glyph(font, char, origin_x, origin_y, canvas_size, threshold):
    """Render a character to 1-bit bitmap and compute LVGL metrics."""
    adv_w_px = font.getlength(char)
    adv_w_lvgl = int(round(adv_w_px * 16))  # LVGL uses 1/16 pixel units

    img = Image.new('L', (canvas_size, canvas_size), 0)
    draw = ImageDraw.Draw(img)
    draw.text((origin_x, origin_y), char, fill=255, font=font, anchor='ls')

    img_bin = img.point(lambda p: 255 if p > threshold else 0)
    bbox = img_bin.getbbox()

    if bbox is None:
        return None, adv_w_lvgl, 0, 0, 0, 0

    px_l, px_t, px_r, px_b = bbox
    box_w = px_r - px_l
    box_h = px_b - px_t
    ofs_x = px_l - origin_x
    ofs_y = origin_y - px_b  # positive = above baseline

    cropped = img_bin.crop(bbox)
    bitmap = bytearray()
    for y in range(box_h):
        for byte_x in range(0, box_w, 8):
            byte_val = 0
            for bit in range(8):
                x = byte_x + bit
                if x < box_w and cropped.getpixel((x, y)) > 0:
                    byte_val |= (1 << (7 - bit))
            bitmap.append(byte_val)

    return bitmap, adv_w_lvgl, box_w, box_h, ofs_x, ofs_y


def generate_font(font_path, font_size, threshold, font_c_name, output_file):
    font = ImageFont.truetype(font_path, font_size)
    ascent, descent = font.getmetrics()

    CANVAS = 64
    ORIGIN_X = 16
    ORIGIN_Y = 40

    CHAR_START = 0x20
    CHAR_END = 0x7F

    glyphs = []
    all_bitmap = bytearray()
    max_top = 0
    max_bottom = 0

    for code in range(CHAR_START, CHAR_END):
        char = chr(code)
        bitmap, adv_w, box_w, box_h, ofs_x, ofs_y = render_glyph(
            font, char, ORIGIN_X, ORIGIN_Y, CANVAS, threshold)

        bitmap_index = len(all_bitmap)
        if bitmap:
            all_bitmap.extend(bitmap)

        glyphs.append({
            'code': code,
            'bitmap_index': bitmap_index,
            'adv_w': adv_w,
            'box_w': box_w,
            'box_h': box_h,
            'ofs_x': ofs_x,
            'ofs_y': ofs_y,
        })

        if box_h > 0:
            glyph_top = ofs_y + box_h
            glyph_bot = max(0, -ofs_y)
            if glyph_top > max_top:
                max_top = glyph_top
            if glyph_bot > max_bottom:
                max_bottom = glyph_bot

    line_height = max_top + max_bottom
    base_line = max_bottom

    # ── Generate C code ──────────────────────────────────────
    L = []
    L.append(f'/* Auto-generated LVGL font: {font_c_name} */')
    L.append(f'/* Source: {os.path.basename(font_path)} size={font_size} threshold={threshold} */')
    L.append(f'/* Line height: {line_height}, Baseline: {base_line} */')
    L.append('')
    L.append('#include "lvgl.h"')
    L.append('')

    # Bitmap data
    L.append('static const uint8_t glyph_bitmap[] = {')
    for g in glyphs:
        if g['box_w'] == 0:
            continue
        ch = chr(g['code'])
        ch_disp = ch if g['code'] >= 0x21 and g['code'] != 0x5C else f"0x{g['code']:02X}"
        L.append(f'    /* U+{g["code"]:04X} "{ch_disp}" */')

        bytes_per_row = (g['box_w'] + 7) // 8
        start = g['bitmap_index']
        for row in range(g['box_h']):
            rs = start + row * bytes_per_row
            re = rs + bytes_per_row
            hexs = ', '.join(f'0x{b:02x}' for b in all_bitmap[rs:re])
            L.append(f'    {hexs},')
        L.append('')
    L.append('};')
    L.append('')

    # Glyph descriptors
    L.append('static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {')
    L.append('    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},')
    for g in glyphs:
        ch = chr(g['code'])
        ch_disp = ch if g['code'] >= 0x21 else 'SPC'
        L.append(f'    {{.bitmap_index = {g["bitmap_index"]}, .adv_w = {g["adv_w"]}, '
                 f'.box_w = {g["box_w"]}, .box_h = {g["box_h"]}, '
                 f'.ofs_x = {g["ofs_x"]}, .ofs_y = {g["ofs_y"]}}},  /* {ch_disp} */')
    L.append('};')
    L.append('')

    # Character map
    num_chars = CHAR_END - CHAR_START
    L.append('static const lv_font_fmt_txt_cmap_t cmaps[] = {')
    L.append('    {')
    L.append(f'        .range_start = {CHAR_START}, .range_length = {num_chars}, .glyph_id_start = 1,')
    L.append('        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,')
    L.append('        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY')
    L.append('    }')
    L.append('};')
    L.append('')

    # Font descriptor
    L.append('static const lv_font_fmt_txt_dsc_t font_dsc = {')
    L.append('    .glyph_bitmap = glyph_bitmap,')
    L.append('    .glyph_dsc = glyph_dsc,')
    L.append('    .cmaps = cmaps,')
    L.append('    .kern_dsc = NULL,')
    L.append('    .kern_scale = 0,')
    L.append('    .cmap_num = 1,')
    L.append('    .bpp = 1,')
    L.append('    .kern_classes = 0,')
    L.append('    .bitmap_format = 0')
    L.append('};')
    L.append('')

    # Public font
    L.append(f'const lv_font_t {font_c_name} = {{')
    L.append('    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,')
    L.append('    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,')
    L.append(f'    .line_height = {line_height},')
    L.append(f'    .base_line = {base_line},')
    L.append('    .subpx = LV_FONT_SUBPX_NONE,')
    L.append('    .underline_position = 0,')
    L.append('    .underline_thickness = 0,')
    L.append('    .dsc = &font_dsc,')
    L.append('};')
    L.append('')

    with open(output_file, 'w', newline='\n') as f:
        f.write('\n'.join(L))

    print(f'Generated: {output_file}')
    print(f'  Font: {os.path.basename(font_path)} at {font_size}px, threshold={threshold}')
    print(f'  Glyphs: {len(glyphs)} (U+{CHAR_START:04X}..U+{CHAR_END-1:04X})')
    print(f'  Bitmap: {len(all_bitmap)} bytes')
    print(f'  Line height: {line_height}, Base line: {base_line}')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Generate LVGL bitmap font from TTF')
    p.add_argument('--font', default='C:/Windows/Fonts/arial.ttf', help='TTF font path')
    p.add_argument('--size', type=int, default=10, help='Font size in pixels')
    p.add_argument('--threshold', type=int, default=80, help='Binarization threshold (0-255, lower=thinner)')
    p.add_argument('--name', default='ot_font_thin8', help='C symbol name')
    p.add_argument('--output', default=None, help='Output C file path')
    args = p.parse_args()

    if args.output is None:
        args.output = args.name + '.c'

    generate_font(args.font, args.size, args.threshold, args.name, args.output)
