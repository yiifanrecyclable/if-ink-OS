#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""从已发布资产和实际镜像生成稳定/测试通道清单；不上传。/ Prepare stable/beta manifests from released assets and images; never upload."""
from __future__ import annotations
import argparse, hashlib, json, re, struct
from pathlib import Path
REPOSITORY_ID=1412628734
PRODUCT='if-ink-os'
PROJECT='Read_Pico_IF'
LAYOUT='if-16m-ota-v1'
MAX_BYTES=4194304
CORE=r'(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})'
BETA=r'-beta(?:\.[1-9][0-9]{0,4}|(?:[1-9][0-9]{0,4})?(?:-r[1-9][0-9]{0,4})?)'

def image_metadata(data: bytes,channel: str='stable') -> dict:
    if channel not in ('stable','beta'):raise ValueError('Unknown update channel')
    if len(data)<288 or len(data)>MAX_BYTES or data[0]!=0xE9 or not 1<=data[1]<=16:
        raise ValueError('Invalid or oversized ESP application image')
    if struct.unpack_from('<H',data,12)[0]!=9 or data[23]!=1:
        raise ValueError('Expected an ESP32-S3 image with appended SHA256')
    if struct.unpack_from('<I',data,32)[0]!=0xABCD5432:
        raise ValueError('Missing application descriptor')
    def field(offset):
        raw=data[offset:offset+32]
        if b'\0' not in raw:raise ValueError('Unterminated image identity')
        return raw.split(b'\0',1)[0].decode('ascii')
    version,project=field(48),field(80)
    position=24;checksum=0xEF
    for i in range(data[1]):
        if position+8>len(data):raise ValueError('Truncated segment header')
        size=struct.unpack_from('<I',data,position+4)[0];position+=8
        if (i==0 and size<256) or position+size>len(data):raise ValueError('Invalid segment size')
        for b in data[position:position+size]:checksum^=b
        position+=size
    check_at=position|15
    if check_at+33!=len(data) or data[check_at]!=checksum or any(data[position:check_at]):
        raise ValueError('Invalid image checksum, padding or length')
    if hashlib.sha256(data[:check_at+1]).digest()!=data[check_at+1:]:
        raise ValueError('Invalid appended image SHA256')
    if project!=PROJECT or not re.fullmatch(CORE+(BETA if channel=='beta' else ''),version):
        raise ValueError(f'Image version must match the {channel} channel and this project')
    return {'version':version,'project':project,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}

def prepare(image: bytes,release: dict,asset_id: str,channel: str='stable') -> dict:
    metadata=image_metadata(image,channel)
    if release.get('draft') is not False or release.get('prerelease') is not (channel=='beta') or release.get('tag_name') not in (metadata['version'],'v'+metadata['version']):
        raise ValueError('Release must be published, match the channel and image version')
    if not re.fullmatch(r'[1-9][0-9]{0,19}',asset_id):raise ValueError('Invalid asset id')
    asset=next((a for a in release.get('assets',[]) if str(a.get('id'))==asset_id),None)
    if not asset or not asset.get('name','').endswith('.bin') or asset.get('state')!='uploaded' or asset.get('size')!=len(image):
        raise ValueError('Select the uploaded application .bin asset, not a ZIP or merged USB image')
    if asset.get('url')!=f'https://api.github.com/repos/yiifanrecyclable/if-ink-OS/releases/assets/{asset_id}':
        # GitHub原始API保留当前仓库名；固定编号由发布方从同一仓库获取元数据。
        # Raw GitHub metadata uses the current repository name; fetch release metadata from the fixed repository ID.
        expected=f'https://api.github.com/repositories/{REPOSITORY_ID}/releases/assets/{asset_id}'
        if asset.get('url')!=expected:raise ValueError('Asset does not belong to this repository')
    if asset.get('digest')!='sha256:'+metadata['sha256']:
        raise ValueError('GitHub asset SHA256 must match the local application image')
    return dict(schema=1,product=PRODUCT,project=PROJECT,layout=LAYOUT,channel=channel,available=True,
                version=metadata['version'],size=len(image),sha256=metadata['sha256'],asset_id=asset_id,
                blob_sha=hashlib.sha1(b'blob '+str(len(image)).encode()+b'\0'+image).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image',type=Path);parser.add_argument('--release-json',required=True,type=Path)
    parser.add_argument('--asset-id',required=True);parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--channel',choices=('stable','beta'),default='stable')
    args=parser.parse_args()
    if args.out.name in ('stable.json','beta.json') and args.out.stem!=args.channel:
        parser.error('Output feed filename must match the selected channel')
    try:feed=prepare(args.image.read_bytes(),json.loads(args.release_json.read_text()),args.asset_id,args.channel)
    except (ValueError,OSError,KeyError,UnicodeError) as error:parser.error(str(error))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(feed,ensure_ascii=False,indent=2)+'\n')
    print(f"Prepared {feed['version']}: {feed['size']} bytes, SHA256 {feed['sha256']}; no upload performed")
if __name__=='__main__':main()
