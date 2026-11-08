"""Bounded CMT3 Canon quality-tag reader, never inferred from file size.

Public field definitions: https://exiftool.org/TagNames/Canon.html
Only CameraSettings Quality and FileInfo RawJpgQuality, not a full MakerNote decoder.
"""
import struct
from pathlib import Path


CANON_UUID=bytes.fromhex('85c0b687820f11e08111f4ce462b6a48')
RAW_QUALITY={4:'RAW',7:'CRAW',130:'Light RAW',131:'Standard RAW'}


def quality_fields(data):
    if len(data)<8 or len(data)>2*1024*1024 or data[:2] not in (b'II',b'MM'):
        raise ValueError('Bounded TIFF CMT3 required')
    order='<' if data[:2]==b'II' else '>'
    if struct.unpack_from(order+'H',data,2)[0]!=42:raise ValueError('Classic TIFF required')
    offset=struct.unpack_from(order+'I',data,4)[0]
    if offset<8 or offset+2>len(data):raise ValueError('IFD outside CMT3')
    count=struct.unpack_from(order+'H',data,offset)[0]
    if count>4096 or offset+2+count*12+4>len(data):raise ValueError('Truncated CMT3 IFD')
    result={}
    for i in range(count):
        pos=offset+2+i*12
        tag,kind,length=struct.unpack_from(order+'HHI',data,pos)
        if tag not in (1,0x93) or kind not in (3,7) or length>1024*1024:continue
        size=length*(2 if kind==3 else 1)
        start=pos+8 if size<=4 else struct.unpack_from(order+'I',data,pos+8)[0]
        if start+size>len(data):raise ValueError('Quality payload outside CMT3')
        index,name=(3,'CameraSettingsQuality') if tag==1 else (6,'RawJpgQuality')
        if size>=(index+1)*2:
            result[name]=struct.unpack_from(order+'h',data,start+index*2)[0]
    return result


def label(fields):
    known={RAW_QUALITY[value] for value in fields.values() if value in RAW_QUALITY}
    return next(iter(known)) if len(known)==1 else 'unknown_or_conflicting'


def read_mode(path):
    path=Path(path)
    before=path.stat()
    found=[]
    with path.open('rb') as stream:
        def walk(start,end,depth):
            if depth>8:raise ValueError('Container depth limit')
            cursor=start
            while cursor<end:
                if end-cursor<8:raise ValueError('Truncated box header')
                stream.seek(cursor);size,kind=struct.unpack('>I4s',stream.read(8));header=8
                if size==1:
                    if end-cursor<16:raise ValueError('Truncated extended box')
                    size=struct.unpack('>Q',stream.read(8))[0];header=16
                if size==0:size=end-cursor
                if size<header or cursor+size>end:raise ValueError('Invalid box extent')
                first,last=cursor+header,cursor+size
                if kind==b'CMT3':
                    if last-first>2*1024*1024:raise ValueError('Oversized CMT3')
                    stream.seek(first);found.append(quality_fields(stream.read(last-first)))
                elif kind==b'moov':walk(first,last,depth+1)
                elif kind==b'uuid' and last-first>=16:
                    stream.seek(first)
                    if stream.read(16)==CANON_UUID:walk(first+16,last,depth+1)
                cursor=last
        walk(0,before.st_size,0)
    fields={}
    for entry in found:
        for key,value in entry.items():
            if key in fields and fields[key]!=value:raise ValueError('Conflicting quality metadata')
            fields[key]=value
    after=path.stat()
    if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):raise ValueError('RAW changed during mode read')
    return dict(fields=fields,mode=label(fields),CMT3_blocks=len(found),source_unchanged=True,
                scope='Canon metadata label only; no assertion about codec error or noise')
