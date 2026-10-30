# SPDX-License-Identifier: Apache-2.0
"""清单发布门槛与真实ESP镜像格式检查。/ Release gates and real ESP image format checks."""
import copy,hashlib,importlib.util,struct
from pathlib import Path
spec=importlib.util.spec_from_file_location('online',Path(__file__).with_name('prepare_online_update.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def image(version='0.1.0',project='Read_Pico_IF'):
    h=bytearray(24);h[0]=0xe9;h[1]=1;h[12]=9;h[23]=1
    d=bytearray(256);struct.pack_into('<I',d,0,0xabcd5432);d[16:16+len(version)]=version.encode();d[48:48+len(project)]=project.encode()
    b=h+struct.pack('<II',0,len(d))+d;checksum=0xef
    for x in d:checksum^=x
    b+=bytes((len(b)|15)-len(b))+bytes([checksum]);return bytes(b)+hashlib.sha256(b).digest()
b=image();asset=dict(id=123,name='if-ink-OS.bin',state='uploaded',size=len(b),digest='sha256:'+hashlib.sha256(b).hexdigest(),url='https://api.github.com/repos/yiifanrecyclable/if-ink-OS/releases/assets/123')
r=dict(tag_name='v0.1.0',draft=False,prerelease=False,assets=[asset]);assert m.prepare(b,r,'123')['available']
def reject(b,r):
    try:m.prepare(b,r,'123')
    except ValueError:return
    raise AssertionError('Invalid publication accepted')
for version in ('0.1.0-test.2','0.3.3-rc83-if.8','01.1.0'):reject(image(version),r)
reject(image(project='Read_Pico'),r);reject(b[:-1],r)
corrupt=bytearray(b);corrupt[300]^=1;reject(corrupt,r)
for key,value in (('draft',True),('prerelease',True),('tag_name','v0.1.1')):
    bad=copy.deepcopy(r);bad[key]=value;reject(b,bad)
for key,value in (('digest','sha256:'+'0'*64),('size',1234),('name','firmware.zip'),('url','https://api.github.com/repos/other/repo/releases/assets/123')):
    bad=copy.deepcopy(r);bad['assets'][0][key]=value;reject(b,bad)
def release_for(data,version):
    release=copy.deepcopy(r);release.update(tag_name='v'+version,prerelease=True)
    release['assets'][0].update(size=len(data),digest='sha256:'+hashlib.sha256(data).hexdigest())
    return release
def reject_beta(data,release):
    try:m.prepare(data,release,'123','beta')
    except ValueError:return
    raise AssertionError('Invalid beta publication accepted')
for version in ('0.1.8-beta.1','0.1.8-beta.10','0.1.8-beta','0.1.8-beta3-r2'):
    data=image(version);release=release_for(data,version)
    feed=m.prepare(data,release,'123','beta');assert feed['channel']=='beta' and feed['version']==version
    reject(data,release)
    release['prerelease']=False;reject_beta(data,release)
for version in ('0.1.8','0.1.8-test.2','0.1.8-beta.0','0.1.8-beta.01','0.1.8-beta.100000','0.1.8-beta.1junk'):
    data=image(version);reject_beta(data,release_for(data,version))
print('PASS: stable release gate, raw application identity, chip/ESP checksum/SHA256, tag/asset/digest mismatch and prerelease rejection')
