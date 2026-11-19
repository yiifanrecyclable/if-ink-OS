#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production preview cache with a third page crossing the byte budget."""
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/font/ttf_font.c").read_text()

def extract(name):
    match = re.search(r"^static [^;\n]+\b" + name + r"\([^;\n]*\) \{", source, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]

types = source[source.index("#define NAME_PREVIEW_SLOTS"):source.index("#ifndef PICO_HOST_SIMULATOR",source.index("#define NAME_PREVIEW_SLOTS"))]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define TTF_FONT_PATH_MAX 288
#define TTF_FONT_NAME_MAX 64
#define heap_caps_free free
static atomic_bool s_name_cancel;
'''
unit += types.replace("static int64_t s_name_idle_since;", "").replace("s_name_revision, ", "")
unit += extract("name_preview_evict") + "\n" + extract("name_preview_collect")
unit += r'''
static name_preview_t *find(unsigned id) {
    for (size_t i=0;i<NAME_PREVIEW_SLOTS;++i)
        if (s_names[i].used && (unsigned)s_names[i].key.px==id) return &s_names[i];
    return NULL;
}

static void push(unsigned id, int stride, int height, bool cancel) {
    size_t bytes = (size_t)stride*height;
    s_name_job = (name_preview_t){.key={.px=(int)id},.stride=stride,.height=height,
                                 .mask=bytes?calloc(1,bytes):NULL};
    assert(!bytes || s_name_job.mask);
    atomic_store(&s_name_cancel, cancel);
    s_name_pending=true;
    name_preview_collect();
    size_t counted=0;
    for(size_t i=0;i<NAME_PREVIEW_SLOTS;++i)
        counted+=(size_t)s_names[i].stride*s_names[i].height;
    assert(!s_name_pending && counted==s_name_bytes && counted<=NAME_PREVIEW_BYTES);
}

int main(void){
    s_names=calloc(NAME_PREVIEW_SLOTS,sizeof(*s_names));
    assert(s_names);
    for(unsigned i=1;i<=18;++i) push(i,284,37,false);
    assert(find(1)&&find(18)&&!s_names[18].used);
    find(1)->used=++s_name_clock;             // A revisited first-page font stays warm.
    push(19,284,37,false);                   // Free slots still exist, but pixel memory is full.
    assert(find(19)&&find(19)->mask&&find(1)&&!find(2));
    push(20,284,37,true);
    assert(!find(20)&&find(19));
    for(unsigned i=21;i<=62;++i) push(i,0,0,false); // Fonts that cannot preview still cache the fallback.
    assert(find(62)&&!find(62)->mask);
    push(63,1024,256,false);                 // Over-budget result must terminate with plain text fallback.
    assert(find(63)&&!find(63)->mask&&!find(63)->height);
    push(64,284,37,false);
    assert(find(64)&&find(64)->mask);
    for(size_t i=0;i<NAME_PREVIEW_SLOTS;++i) name_preview_evict(&s_names[i]);
    assert(!s_name_bytes);
    free(s_names);
    puts("PASS third-page byte limit, LRU, cancellation, slot-only fallback, and oversized result");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="font-cache-") as directory:
    source = Path(directory) / "test.c"
    binary = source.with_suffix("")
    source.write_text(unit)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                    str(source),"-o",str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=3)
