"""Read-only inspector for current IPR sidecar v6, not a historical export recipe."""
import argparse
import json
from pathlib import Path
import struct
from analysis import native


class Reader:
    def __init__(self,data):self.data,self.offset=data,0
    def number(self,fmt):
        size=struct.calcsize('>'+fmt)
        if self.offset+size>len(self.data):raise ValueError('Truncated sidecar')
        value=struct.unpack_from('>'+fmt,self.data,self.offset)[0];self.offset+=size;return value
    def string(self):
        count=self.number('I')
        if count==0xffffffff:return ''
        if count%2 or count>65536 or self.offset+count>len(self.data):raise ValueError('Invalid QString')
        value=self.data[self.offset:self.offset+count].decode('utf-16-be');self.offset+=count;return value
    def boolean(self):
        value=self.number('B')
        if value not in (0,1):raise ValueError('Invalid boolean')
        return bool(value)


def inspect(path):
    data=path.read_bytes()
    if len(data)>1024*1024:raise ValueError('Sidecar too large')
    r=Reader(data)
    if r.number('I')!=0x49505231 or r.number('H')!=6:raise ValueError('Current sidecar v6 required')
    size,modified=r.number('q'),r.number('q')
    settings=dict(brightness=r.number('d'),contrast=r.number('i'),blur=r.number('i'),grayscale=r.boolean())
    for name in ('shadows','blacks','highlights','whites','temperature','tint','vibrance','saturation','texture','clarity','dehaze','grain',
                 'denoiseLuminance','denoiseChrominance','sharpeningAmount'):
        settings[name]=r.number('i')
    settings['sharpeningRadius']=r.number('d');settings['sharpeningMasking']=r.number('i')
    settings['mixer']=[[r.number('i') for _ in range(3)] for _ in range(8)]
    settings['grading']=[[r.number('i') for _ in range(2)] for _ in range(3)]
    settings['automaticWhiteBalance']=r.boolean();curves=[]
    for _ in range(4):
        count=r.number('i')
        if not 0<=count<=64:raise ValueError('Invalid curve count')
        curves.append([[r.number('d'),r.number('d')] for _ in range(count)])
    settings['curves']=curves
    user=dict(rating=r.number('i'),colorLabel=r.string(),flagged=r.boolean(),ratingSet=r.boolean(),colorLabelSet=r.boolean())
    raw={name:r.string() for name in ('profile','profileSha256','manifest','manifestSha256')}
    raw['normalizationEnabled']=r.boolean();raw['engineVersion']=r.string()
    raw['lensProfile']=r.string();raw['lensProfileSha256']=r.string()
    for name in ('lensProfileResolved','lensCorrectionEnabled','chromaticAberrationEnabled'):raw[name]=r.boolean()
    if r.offset!=len(data):raise ValueError('Unparsed sidecar bytes')
    source=Path(str(path)[:-4]);stat=source.stat()
    return dict(file=str(path),sha256=native.sha256(path),source_signature_matches=(size==stat.st_size and modified==stat.st_mtime_ns//1000000),
                source_size=size,source_modified_ms=modified,settings=settings,raw=raw,user_metadata=user,
                limitation='Current saved state only, not proof of settings at TIFF export time')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('sidecars',type=Path,nargs='+');args=parser.parse_args()
    print(json.dumps([inspect(path) for path in args.sidecars],indent=2))
