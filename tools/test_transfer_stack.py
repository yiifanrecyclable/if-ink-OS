#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check web-editor application frame budgets in the actual Xtensa ELF (not a host-stack estimate).
This guards large local buffers/inlining regressions, not the whole call graph or a physical crash diagnosis.
"""
import argparse, json, re, subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf',type=Path,required=True)
p.add_argument('--objdump',required=True)
args=p.parse_args()
budgets={'transfer_appearance_dispatch':128,'appearance_edit':2600,'same_file':512}
frames={}
for name,limit in budgets.items():
 text=subprocess.check_output([args.objdump,'-d','--disassemble='+name,str(args.elf)],text=True)
 start=re.search(r'^[0-9a-f]+ <'+re.escape(name)+r'>:\n\s*[0-9a-f]+:\s+[0-9a-f]+\s+entry\s+a1,\s*(0x[0-9a-f]+|\d+)',text,re.M)
 assert start, 'Missing Xtensa entry for '+name
 frames[name]=int(start[1],0)
 assert frames[name]<=limit,(name,frames[name],limit)
print(json.dumps({'frames_bytes':frames,'budgets_bytes':budgets,'scope':'Application frame limits only; callbacks/libc/indirect calls and real hardware still require validation.'},indent=2))
