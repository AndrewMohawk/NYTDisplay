#!/usr/bin/env python3
"""Validate the packed background and scan a QR from the actual firmware library.

Requires a C compiler, Pillow and zxing-cpp. No hardware or internet required.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image
import zxingcpp

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = 'WIFI:T:WPA;S:NYTDisplay;P:SampleKey2026;;'


def main():
    for name in ('welcome_background.h', 'loading_background.h'):
        header = (ROOT / 'firmware/include' / name).read_text()
        encoded = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', header))
        assert len(encoded) % 2 == 0
        decoded = bytearray()
        for count, value in zip(encoded[::2], encoded[1::2]):
            assert count > 0 and value >> 4 < 6 and value & 15 < 6
            decoded.extend([value] * count)
        assert len(decoded) == 1200 * 1600 // 2
    loading = Image.open(ROOT / 'assets/loading-screen-preview.png')
    assert loading.size == (1200, 1600)
    assert zxingcpp.read_barcode(loading) is None
    preview = Image.open(ROOT / 'assets/setup-preview.png')
    assert preview.size == (1200, 1600)
    assert zxingcpp.read_barcode(preview).text == PAYLOAD

    # Use the exact vendored C encoder, QR version, correction and scale that
    # welcome.cpp uses, then independently decode the resulting raster.
    source = r'''
    #include <stdio.h>
    #include <stdint.h>
    #include "qrcode.h"
    int main(void) {
      QRCode qr;
      uint8_t data[1024];
      if (qrcode_getBufferSize(6) > sizeof(data)) return 1;
      if (qrcode_initText(&qr, data, 6, ECC_MEDIUM,
                         "WIFI:T:WPA;S:NYTDisplay;P:SampleKey2026;;")) return 2;
      int size = (qr.size + 8) * 8;
      printf("P5\n%d %d\n255\n", size, size);
      for (int y=0; y<size; ++y) for (int x=0; x<size; ++x) {
        int qx=x/8-4, qy=y/8-4;
        int dark=qx>=0 && qy>=0 && qx<qr.size && qy<qr.size &&
                 qrcode_getModule(&qr,qx,qy);
        putchar(dark ? 0 : 255);
      }
      return 0;
    }
    '''
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        (temp / 'check.c').write_text(source)
        lib = ROOT / 'firmware/lib/QRCode'
        subprocess.run(['cc', '-std=c99', '-I', str(lib), str(temp / 'check.c'),
                        str(lib / 'qrcode.c'), '-o', str(temp / 'check')], check=True)
        with (temp / 'qr.pgm').open('wb') as out:
            subprocess.run([str(temp / 'check')], stdout=out, check=True)
        actual = Image.open(temp / 'qr.pgm')
        assert actual.size == (392, 392)
        assert zxingcpp.read_barcode(actual).text == PAYLOAD
    print('PASS: both backgrounds decode to 960000 valid packed bytes; loading has no QR, and setup preview plus firmware QR scan to NYTDisplay with the expected password.')


if __name__ == '__main__':
    main()
