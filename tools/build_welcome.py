#!/usr/bin/env python3
"""Build the generic e-paper setup and loading screens.

QR data and setup password are added by firmware at runtime, never baked into
the setup background. Its preview uses a clearly documented sample password.
"""
from pathlib import Path
from itertools import groupby
import argparse
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 1200, 1600
# Same nominal ink colors used by firmware/src/jpeg_render.cpp.
PALETTE = [(0, 0, 0), (255, 255, 255), (29, 185, 84),
           (0, 76, 255), (229, 57, 53), (255, 216, 0)]


def font(name, size):
    linux = Path('/usr/share/fonts/truetype/dejavu') / f'{name}.ttf'
    if linux.exists():
        return ImageFont.truetype(linux, size)
    mac_names = {
        'DejaVuSerif-Bold': 'Georgia Bold.ttf',
        'DejaVuSans': 'Arial.ttf',
        'DejaVuSans-Bold': 'Arial Bold.ttf',
        'DejaVuSansMono-Bold': 'Andale Mono.ttf',
    }
    mac = Path('/System/Library/Fonts/Supplemental') / mac_names[name]
    return ImageFont.truetype(mac, size)


def pack_and_write(image, art_bounds, asset_name, header_name, symbol):
    """Quantize an image to the panel palette and emit its RLE firmware header."""
    palette = Image.new('P', (1, 1))
    palette.putpalette([c for rgb in PALETTE for c in rgb] + [255] * (768 - 18))
    quantized = image.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
    # Pillow can select duplicated white entries; normalize all unused indices.
    indices = bytearray(quantized.tobytes().translate(bytes(list(range(6)) + [1] * 250)))
    art_x, art_y, art_w, art_h = art_bounds
    pixels = image.get_flattened_data() if hasattr(image, 'get_flattened_data') else image.getdata()
    for i, (r, g, b) in enumerate(pixels):
        y, x = divmod(i, WIDTH)
        if not (art_x <= x < art_x + art_w and art_y <= y < art_y + art_h):
            indices[i] = 1 if r * 299 + g * 587 + b * 114 > 175000 else 0
        elif min(r, g, b) >= 242:
            indices[i] = 1
        elif max(r, g, b) <= 12:
            indices[i] = 0
    packed = bytes((indices[i] << 4) | indices[i + 1] for i in range(0, len(indices), 2))
    rle = bytearray()
    for value, values in groupby(packed):
        count = sum(1 for _ in values)
        while count:
            part = min(count, 255)
            rle.extend((part, value))
            count -= part
    indexed = Image.frombytes('P', (WIDTH, HEIGHT), bytes(indices))
    indexed.putpalette([c for rgb in PALETTE for c in rgb] + [255] * (768 - 18))
    indexed.save(ROOT / 'assets' / asset_name)
    header = '#pragma once\n#include <Arduino.h>\n// RLE count,value pairs for a 960000-byte packed frame.\n'
    header += f'static const uint8_t {symbol}[] PROGMEM = {{\n'
    header += '\n'.join('  ' + ','.join(f'0x{b:02x}' for b in rle[i:i+24]) + ',' for i in range(0,len(rle),24))
    header += '\n};\n'
    (ROOT / 'firmware/include' / header_name).write_text(header)
    print(f'{asset_name}: {len(packed)} bytes -> {len(rle)} bytes of RLE')
    return indexed.convert('RGB')


def build_setup():
    image = Image.new('RGB', (WIDTH, HEIGHT), 'white')
    draw = ImageDraw.Draw(image)
    size = 92
    while draw.textlength('The Daily Front Page', font=font('DejaVuSerif-Bold', size)) > 1080:
        size -= 1
    title = font('DejaVuSerif-Bold', size)
    draw.text((WIDTH // 2, 75), 'The Daily Front Page', font=title, fill='black', anchor='mt')
    draw.text((WIDTH // 2, 194), 'New York Times print edition',
              font=font('DejaVuSans', 32), fill='black', anchor='mt')
    art = Image.open(ROOT / 'assets/newspaper-art.png').convert('RGB')
    art = ImageOps.contain(art, (730, 730), Image.Resampling.LANCZOS)
    art_x, art_y = (WIDTH - art.width) // 2, 270
    image.paste(art, (art_x, art_y))
    draw.line((76, 1010, 1124, 1010), fill='black', width=2)
    draw.text((76, 1043), "LET'S GET YOU CONNECTED", font=font('DejaVuSans-Bold', 31), fill='black', anchor='lt')
    draw.text((535, 1115), '1  Scan to join Wi-Fi.', font=font('DejaVuSans-Bold', 33), fill='black', anchor='lt')
    draw.text((535, 1174), 'Network: NYTDisplay', font=font('DejaVuSans', 29), fill='black', anchor='lt')
    draw.text((535, 1225), 'Password:', font=font('DejaVuSans', 28), fill='black', anchor='lt')
    # Password area y1260..1310 and QR box x80,y1110,size392 stay blank.
    draw.text((535, 1334), '2  Open the setup page.', font=font('DejaVuSans-Bold', 32), fill='black', anchor='lt')
    draw.text((535, 1387), 'http://192.168.4.1', font=font('DejaVuSansMono-Bold', 31), fill='black', anchor='lt')
    draw.text((535, 1445), 'Choose your home Wi-Fi.', font=font('DejaVuSans', 29), fill='black', anchor='lt')
    draw.text((535, 1490), '2.4 GHz networks only.', font=font('DejaVuSans', 26), fill='black', anchor='lt')
    draw.text((276, 1522), 'Scan with your phone camera', font=font('DejaVuSans', 22), fill='black', anchor='mt')
    draw.text((600, 1564), 'If asked, stay connected without internet.', font=font('DejaVuSans', 24), fill='black', anchor='mt')

    return pack_and_write(image, (art_x, art_y, art.width, art.height),
                          'welcome-background.png',
                          'welcome_background.h', 'WELCOME_BACKGROUND_RLE')


def build_loading():
    image = Image.new('RGB', (WIDTH, HEIGHT), 'white')
    draw = ImageDraw.Draw(image)
    size = 70
    message = 'Fetching the front page...'
    while draw.textlength(message, font=font('DejaVuSerif-Bold', size)) > 1080:
        size -= 1
    draw.text((WIDTH // 2, 80), message,
              font=font('DejaVuSerif-Bold', size), fill='black', anchor='mt')
    draw.text((WIDTH // 2, 185), 'Saving five recent editions',
              font=font('DejaVuSans', 32), fill='black', anchor='mt')

    art = Image.open(ROOT / 'assets/newspaper-art.png').convert('RGBA')
    art = ImageOps.contain(art, (920, 960), Image.Resampling.LANCZOS)
    art_x, art_y = (WIDTH - art.width) // 2, 270
    image.paste(art, (art_x, art_y), art)

    # Firmware draws the live five-step bar and count in this reserved area.
    draw.rounded_rectangle((90, 1260, 1110, 1380), radius=20,
                           outline='black', width=8)
    draw.text((WIDTH // 2, 1430), 'Please keep me powered on',
              font=font('DejaVuSans-Bold', 32), fill='black', anchor='mt')
    return pack_and_write(image, (art_x, art_y, art.width, art.height),
                          'loading-screen-preview.png',
                          'loading_background.h', 'LOADING_BACKGROUND_RLE')


def preview(background, password):
    import qrcode
    qr = qrcode.QRCode(version=6, error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=8, border=4)
    qr.add_data(f'WIFI:T:WPA;S:NYTDisplay;P:{password};;', optimize=0)
    qr.make(fit=False)
    background.paste(qr.make_image(fill_color='black', back_color='white').convert('RGB'), (80, 1110))
    ImageDraw.Draw(background).text((535, 1262), password,
        font=font('DejaVuSansMono-Bold', 35), fill='black', anchor='lt')
    background.save(ROOT / 'assets/setup-preview.png')
    print('Preview password is a sample; the device generates its own private setup password.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview-password', default='SampleKey2026')
    args = parser.parse_args()
    preview(build_setup(), args.preview_password)
    build_loading()
