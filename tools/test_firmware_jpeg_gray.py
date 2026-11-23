#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the firmware JPEG path with the actual JPEGDEC decoder on the host."""
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <assert.h>

esp_err_t esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    (void)cfg;
    (void)info;
    assert(0 && "the ROM path should not receive baseline grayscale or progressive JPEG");
    return ESP_FAIL;
}

esp_err_t esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    return esp_jpeg_get_image_info(cfg, info);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    unsigned w, h;
    unsigned char sof, components;
    assert(book_jpeg_frame(data, (size_t)length, &sof, &w, &h, &components));
    assert((sof == 0xc0 && components == 1) || sof == 0xc2);
    uint8_t *gray = malloc((size_t)w * h);
    assert(gray && jpeg_gray(data, (size_t)length, w, h, gray));
    file = fopen(argv[2], "wb");
    assert(file && fwrite(gray, 1, (size_t)w * h, file) == (size_t)w * h && fclose(file) == 0);
    free(gray);
    free(data);
}
"""


def main():
    # The simulator uses macOS image services, so compile firmware routing separately.
    cover = (ROOT / "main/book/book_cover.c").read_text()
    jpeg = cover.split("typedef struct { const uint8_t *data; size_t size, pos; } png_input_t;", 1)[0]
    jpeg = jpeg.replace('#include "png.h"\n', "")
    decoder = ROOT / "components/jpegdec/src"
    rom_interface = ROOT / "managed_components/espressif__esp_jpeg/include"
    with tempfile.TemporaryDirectory(prefix="firmware-jpeg-") as directory:
        work = Path(directory)
        unit = work / "jpeg.c"
        unit.write_text(jpeg + HARNESS)
        obj = work / "jpeg.o"
        tool = work / "jpeg"
        subprocess.run(["cc", "-std=gnu11", "-I" + str(decoder), "-I" + str(rom_interface),
                        "-I" + str(ROOT / "tools/simulator/include"), "-I" + str(ROOT / "main/book"),
                        "-c", str(unit), "-o", str(obj)], check=True)
        subprocess.run(["c++", "-std=c++11", str(obj), str(decoder / "JPEGDEC.cpp"),
                        str(decoder / "jpegdec_shim.cpp"), "-o", str(tool)], check=True)
        gray = Image.new("L", (378, 409), 245)
        drawing = ImageDraw.Draw(gray)
        drawing.rectangle((16, 16, 80, 80), fill=15)
        drawing.rectangle((320, 320, 377, 408), fill=185)
        for progressive in (False, True):
            source = work / "fixture.jpg"
            output = work / "decoded.gray"
            gray.save(source, quality=88, progressive=progressive)
            subprocess.run([str(tool), str(source), str(output)], check=True)
            result = Image.frombytes("L", gray.size, output.read_bytes())
            assert result.getpixel((40, 40)) < 30
            assert result.getpixel((200, 200)) > 230
            assert 165 < result.getpixel((377, 408)) < 205
    print("PASS: production grayscale JPEG routing, baseline/progressive and odd right/bottom edges")


if __name__ == "__main__":
    main()
