# SPDX-License-Identifier: Apache-2.0
"""Check row orientation, scaling and GIF first-frame decoding with synthetic images."""
from pathlib import Path
import subprocess, tempfile
from random import Random
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='image-orientation-') as temp:
    work=Path(temp)
    source=work/'test.c'
    source.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include "book_cover.h"
#include "esp_err.h"
esp_err_t book_epub_cover(const char*p,uint8_t**d,size_t*n,bool*png){(void)p;(void)d;(void)n;(void)png;return ESP_FAIL;}
bool book_auto_cover_render(const char*p,const char*t,const char*a,unsigned w,unsigned h,uint8_t*out){(void)p;(void)t;(void)a;(void)w;(void)h;(void)out;return false;}
int main(int argc, char **argv) {
    if (argc != 4) return 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    rewind(f);
    uint8_t *data = malloc(n);
    if (!data || fread(data, 1, n, f) != n) return 3;
    fclose(f);
    unsigned w = atoi(argv[2]), h = atoi(argv[3]);
    uint8_t *out = malloc(w * h);
    bool ok = out && book_image_grayscale(data, n, data[0] == 137, w, h, out);
    if (ok) fwrite(out, 1, w * h, stdout);
    free(data);
    free(out);
    return ok ? 0 : 4;
}
''')
    binary=work/'decode'
    subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined',
        '-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main/book'),
        str(source),str(ROOT/'tools/simulator/image_decode.c'),str(ROOT/'main/book/book_gif.c'),
        '-framework','CoreFoundation','-framework','CoreGraphics','-framework','ImageIO','-o',str(binary)],check=True)
    im=Image.new('RGB',(64,64));pix=im.load()
    for y in range(64):
        for x in range(64):
            g=(30 if x<32 else 80) if y<32 else (160 if x<32 else 230)
            pix[x,y]=(g,g,g)
    for ext in ('png','jpg'):
        path=work/f'quadrants.{ext}';im.save(path,quality=100)
        for w,h in ((64,64),(32,32),(48,24),(24,48)):
            out=subprocess.check_output([str(binary),str(path),str(w),str(h)])
            samples=[out[y*w+x] for x,y in ((w//4,h//4),(3*w//4,h//4),(w//4,3*h//4),(3*w//4,3*h//4))]
            assert all(abs(a-b)<=4 for a,b in zip(samples,(30,80,160,230))),(ext,w,h,samples)
    rng = Random(1)
    im = Image.new('P', (129, 109))
    im.putpalette([g for g in range(256) for _ in range(3)])
    im.putdata([rng.randrange(256) for _ in range(im.width * im.height)])
    gif = work / 'first-frame.gif'
    im.save(gif, interlace=True, transparency=3, optimize=False)
    encoded = gif.read_bytes()
    descriptor = encoded.index(b'\x2c\0\0\0\0\x81\0\x6d\0', 13)
    assert encoded[descriptor + 9] & 0x40
    pixels = im.load()
    for w, h in ((129, 109), (62, 53), (180, 160)):
        out = subprocess.check_output([str(binary), str(gif), str(w), str(h)])
        expected = [pixels[x * im.width // w, y * im.height // h] for y in range(h) for x in range(w)]
        assert out == bytes(255 if g == 3 else g for g in expected)
    corrupt = work / 'missing-end-block.gif'
    corrupt.write_bytes(gif.read_bytes()[:-2])
    bad = subprocess.run([str(binary), str(corrupt), '129', '109'], capture_output=True)
    assert bad.returncode == 4 and bad.stdout == b'' and bad.stderr == b''
    animation = work / 'animation.gif'
    im.save(animation, save_all=True, append_images=[Image.new('L', im.size, 0)], optimize=False)
    assert subprocess.check_output([str(binary), str(animation), '129', '109']) == im.tobytes()
    print('PASS: PNG/JPEG orientation and GIF scaling, transparency, interlace, truncation and first frame.')
