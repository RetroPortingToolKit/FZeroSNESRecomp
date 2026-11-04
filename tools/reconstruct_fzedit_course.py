"""Recover an editable FZEdit project from a reviewed FZCOURSE resource.

The editor files are reconstructed, not the author's original project. A
data-only companion preserves native details while the corresponding editor
inputs are unchanged (including layout compression and record identity).
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import uuid
import xml.etree.ElementTree as ET
import zipfile

from PIL import Image
from pack_manifest import write_index

SONGS = [1, 3, 2, 4, 9, 5, 8, 0, 6, 7]
ALPHABET = [0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x8e,0x6c,0x6e,0x6f,
            0x8a,0x8b,0x8c,0x8d,0x6d,0x8f,0xa0,0xa1,0xa2,0xa3,0xa4,0xa6,0xa5,0xa7]
BITS = {'MAIN':(0,128),'BRANCH':(0,64),'DIRT':(256,128),'JUMP':(256,64),
        'LANDMINE':(256,32),'DASH':(256,16),'DOWNPULL_MAGNET':(256,8),
        'MAGNET':(256,4),'ICE':(512,128),'RIGHT_PUSH':(512,64),
        'LEFT_PUSH':(512,32),'PIT':(512,16),'CUSTOM1':(512,1),
        'CUSTOM2':(512,2),'CUSTOM3':(512,4),'CUSTOM4':(512,8),
        'BARRIER':(768,16),'WALL':(768,32),'BACKGROUND':(768,128)}


def sections(data):
    if data[:9] != b'FZCOURSE\1':
        raise ValueError('Unsupported course resource')
    result, at = {}, 9
    while at < len(data):
        key, size = struct.unpack_from('<HH', data, at)
        at += 4
        if key in result or at + size > len(data):
            raise ValueError('Invalid resource section')
        result[key] = data[at:at+size]
        at += size
    if set(result) != set(range(1,19)):
        raise ValueError('Missing resource section')
    return result


def word(data, at=0):
    return struct.unpack_from('<H', data, at)[0]


def unpack_layout(s):
    pixels = bytearray(1024*512)
    animated = bytes([154,172,174,204,174,205,154,173,172,154,206,175,207,175,173,154])
    for cy in range(16):
        for cx in range(32):
            block = 512 + s[2][cy*32+cx]*32
            for y in range(16):
                row = word(s[2], block+y*2)-0x7000
                for x in range(16):
                    offset = word(s[3], row+x*2)
                    quad = animated[offset-0x3ff0:offset-0x3ff0+4] if 0x3ff0 <= offset <= 0x3ffc else s[1][offset:offset+4]
                    if len(quad) != 4:
                        raise ValueError('Invalid map tile')
                    at = (cy*32+y*2)*1024+cx*32+x*2
                    for index, value in zip((at, at+1024, at+1, at+1025), quad):
                        pixels[index] = value
    return pixels


def xml_bytes(root):
    ET.indent(root)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def reconstruct(resource, output, ident, name, author, credits=b''):
    s = sections(Path(resource).read_bytes())
    m = s[17]
    stem = Path(output).stem
    files = {}
    def add(suffix, data):
        filename = stem+suffix
        files[filename] = data.encode() if isinstance(data,str) else data
        return filename
    def save(suffix, image, fmt):
        buffer = io.BytesIO()
        image.save(buffer, format=fmt, optimize=False)
        return add(suffix, buffer.getvalue())
    palette = [tuple(((word(s[5],i*2) >> shift)&31)*8 for shift in (0,5,10)) for i in range(112)]
    pal = Image.new('RGB',(16,7)); pal.putdata(palette)
    pal_file = save('_Palette.bmp',pal,'BMP')
    def sheet(data, planar=False):
        pixels = bytearray(128*128)
        for tile in range(256):
            for y in range(8):
                for x in range(8):
                    value = sum(((data[tile*32+(bit//2)*16+y*2+bit%2] >> (7-x))&1)<<bit for bit in range(4)) if planar else data[tile*64+y*8+x]
                    pixels[(tile//16*8+y)*128+tile%16*8+x] = value
        image = Image.frombytes('P',(128,128),bytes(pixels))
        colors = [(0,0,0)]*16 + palette + [(0,0,0)]*(240-len(palette))
        image.putpalette([c for color in colors for c in color])
        return image
    tiles = sheet(s[4])
    tiles_file = save('_Tileset.gif',tiles,'GIF')
    save('_Tilemap.png',tiles.resize((256,256),Image.Resampling.NEAREST),'PNG')
    horizon = sheet(s[6],True)
    horizon.putpalette([c for color in palette[80:96]+[(0,0,0)]*240 for c in color])
    horizon_file = save('_Horizon_Tilemap.gif',horizon,'GIF')
    horizon_preview = Image.new('RGB',(128,256))
    horizon_preview.paste(horizon.convert('RGB'),(0,0))
    alternate = horizon.copy()
    alternate.putpalette([c for color in palette[96:112]+[(0,0,0)]*240 for c in color])
    horizon_preview.paste(alternate.convert('RGB'),(0,128))
    save('_Horizon_Tilemap.png',horizon_preview,'PNG')
    tsx = ET.Element('tileset',name=stem,tilewidth='16',tileheight='16',tilecount='256',columns='16')
    ET.SubElement(tsx,'image',source=stem+'_Tilemap.png',width='256',height='256')
    for tile in range(256):
        props = ET.SubElement(ET.SubElement(tsx,'tile',id=str(tile)),'properties')
        for key,(offset,mask) in BITS.items():
            ET.SubElement(props,'property',name=key,value=str(int(bool(s[10][offset+tile]&mask))))
    tsx_file = add('_Tilemap.tsx',xml_bytes(tsx))
    horizon_tsx = ET.Element('tileset',name=stem+'_Horizon',tilewidth='8',tileheight='8',tilecount='512',columns='16')
    ET.SubElement(horizon_tsx,'image',source=stem+'_Horizon_Tilemap.png',width='128',height='256')
    add('_Horizon_Tilemap.tsx',xml_bytes(horizon_tsx))
    def tiled(width,height,tilewidth,tsx_name):
        root = ET.Element('map',version='1.0',tiledversion='1.1.5',orientation='orthogonal',renderorder='right-down',width=str(width),height=str(height),tilewidth=str(tilewidth),tileheight=str(tilewidth),infinite='0')
        ET.SubElement(root,'tileset',firstgid='1',source=tsx_name)
        return root
    def layer(root,label,width,height,values):
        node = ET.SubElement(root,'layer',name=label,width=str(width),height=str(height))
        ET.SubElement(node,'data',encoding='csv').text = '\n'+',\n'.join(','.join(str(v) for v in values[y*width:(y+1)*width]) for y in range(height))+'\n'
    track = tiled(1024,512,16,tsx_file)
    layer(track,'Track',1024,512,[v+1 for v in unpack_layout(s)])
    objects = ET.SubElement(track,'objectgroup',name='Shortcuts')
    for i in range(17):
        row = s[14][i*17:]
        if word(row)&0x8000: break
        for label,offset in [('J',0),('L',8)]:
            x,y,x2,y2 = struct.unpack_from('<4H',row,offset)
            if x2<x or y2<y: raise ValueError('Wrapped shortcut rectangle')
            obj = ET.SubElement(objects,'object',id=str(i*2+(1 if label=='J' else 2)),name=f'{label}_{i}',x=str(x*2),y=str(y*2),width=str((x2-x)*2),height=str((y2-y)*2))
            ET.SubElement(ET.SubElement(obj,'properties'),'property',name='checkpoint',value=str(row[16]))
    track_file = add('_Tilemap.tmx',xml_bytes(track))
    sky = tiled(112,7,8,stem+'_Horizon_Tilemap.tsx')
    for label,section,width in [('Background',8,96),('Foreground',7,112)]:
        values=[]
        for y in range(7):
            for x in range(width):
                v=word(s[section],((x//32)*224+y*32+x%32)*2)
                tile=v&1023
                gid=0 if tile==384 else (tile&255)+1+(256 if ((v>>10)&7)==7 else 0)
                values.append(gid|((v>>14&1)<<31)|((v>>15&1)<<30))
        layer(sky,label,width,7,values)
    sky_file=add('_Horizon_Tilemap.tmx',xml_bytes(sky))
    offsets=[0,256,64,320,128,384,192,448]
    mini=Image.new('RGB',(32,64)); pixels=[]
    for y in range(64):
        for x in range(32):
            at=(x//8)*16+offsets[y//8]+y%8*2
            v=sum(((s[9][at+bit]>>(7-x%8))&1)<<bit for bit in range(2))
            pixels.append([(255,0,0),(0,0,0),(255,255,255),(0,255,0)][v])
    mini.putdata(pixels); mini_file=save('_Minimap.bmp',mini,'BMP')
    path=s[12]
    def checkpoint_chunk(label,start,end):
        points=[word(path,(i+1)*2+j) for i in range(start,end) for j in (0,512)]
        rows=[label+':',f'ORIGIN:{word(path,start*2)},{word(path,512+start*2)}','XY:'+','.join(map(str,points))]
        for key,offset in [('PATH',0x502),('MAIN',0x602),('GREEN',0x702),('PURPLE',0x802)]:
            rows.append(key+':'+','.join(map(str,path[offset+start:offset+end])))
        return '\n'.join(rows)+'\n'
    aip=checkpoint_chunk('MAIN_PATH',0,m[6]+1)
    if m[7]:
        # The extracted structure has no branch length. Find the last stored
        # endpoint; zero-filled tail is not an authored checkpoint.
        end=max(i for i in range(m[7]+1,256) if word(path,i*2) or word(path,512+i*2))
        aip+=checkpoint_chunk('BRANCH_PATH',m[7],end)
    # FZEdit's own reader searches for literal CRLF separators.
    aip_file=add('_Checkpoints.aip',aip.replace('\n','\r\n'))
    alphabet=dict(zip(ALPHABET,'ABCDEFGHIJKLMNOPQRSTUVWXYZ'))
    alphabet.update({0x80+i:str(i) for i in range(10)})
    alphabet.update({0xff:' ',0x1b:'.',0x28:"'",0x38:'"',0xa9:'?',0x2a:'(',0x3a:')',0x2b:'^',0xfe:'$'})
    intro=''.join(alphabet.get(v,'?') for v in s[11][6:].split(b'\0')[0])[:26].rstrip()
    props=dict(Version='FZEdit Version 0.9',TrackFile=track_file,AiPathFile=aip_file,TilesetFile=tiles_file,PaletteFile=pal_file,MapName=stem,Variation='0',Venue=str(m[0]&15),Music=str(SONGS.index(m[18]//9) if m[17] else min(m[0]&15,9)),InGameMapName=intro or name.upper()[:26],HorizonShade={35:'Light',163:'Dark'}.get(m[1],'Shadeless'),CycleData=''.join('1' if i*16 in s[15][:m[15]] else '0' for i in range(14)),GUID=str(uuid.uuid5(uuid.NAMESPACE_URL,'fzero:'+ident)),TilesetTSX=tsx_file,HorizonTileMapFile=sky_file,HorizonTileSetFile=horizon_file,MiniMapXOffset=str(word(m,2)),MiniMapYOffset=str(word(m,4)),BeginnerExplosiveFrequency=str(s[13][0]),StandardExplosiveFrequency=str(s[13][1]),ExpertExplosiveFrequency=str(s[13][2]),MiniMapFile=mini_file,ReconstructionFile=stem+'_Reconstruction.json')
    groups={}
    def group(label, file_keys, prop_keys, fields):
        groups[label]=dict(files={k:hashlib.sha256(files[props[k]]).hexdigest() for k in file_keys},properties={k:props[k] for k in prop_keys},fields=fields)
    def raw(**values): return {k:v.hex() for k,v in values.items()}
    group('layout',['TrackFile'],[],dict(**raw(pool=s[1],blocks=s[2],grid=s[3]),block_size=word(m,10),grid_size=word(m,12)))
    group('shortcuts',['TrackFile'],[],raw(shortcuts=s[14]))
    group('checkpoints',['AiPathFile'],[],dict(**raw(path=s[12]),last_checkpoint=m[6],finish_checkpoint=m[7],pit_checkpoint=m[8],has_pit=m[9]))
    group('terrain',['TilesetTSX'],[],raw(terrain=s[10]))
    group('horizon_tiles',['HorizonTileSetFile'],[],raw(sky_graphics=s[6]))
    group('horizon_map',['HorizonTileMapFile'],[],raw(sky_back=s[7],sky_front=s[8]))
    group('intro',[],['InGameMapName'],dict(**raw(name=s[11],intro_glyphs=s[18][1:]),intro_glyph_count=s[18][0]))
    group('setting',[],['Music','Venue','Variation'],dict(setting=m[0]))
    group('gradient',[],['HorizonShade'],dict(gradient=m[1]))
    group('cycles',[],['CycleData'],dict(**raw(palette_cycles=s[15]),has_palette_cycles=m[14],palette_cycle_count=m[15]))
    add('_Reconstruction.json',json.dumps(dict(format='fzero.reconstruction',version=1,origin='Reconstructed from reviewed runtime data; not the original author project.',groups=groups),indent=2)+'\n')
    add('.fzm',''.join(f'{k}={v}\n' for k,v in props.items()))
    requirements=[label for bit,label in [(1,'grip-magnets'),(2,'up-magnets'),(4,'rainbow-road'),(8,'soft-horizon-fog')] if m[16]&bit]
    index=dict(format=1,id=ident,name=name,author=author,cups=[dict(id='course',name=name,courses=['course'])],courses=[dict(id='course',name=name,source=stem+'.fzm',requires=requirements)])
    with tempfile.TemporaryDirectory() as tmp:
        write_index(tmp,index)
        for filename in ['pack.json','courses.json']: files[filename]=(Path(tmp)/filename).read_bytes()
    files['CREDITS.txt']=credits
    files['README.txt']=(f'{name}\n\nReconstructed FZEdit project, not the original author files.\nOpen {stem}.fzm in FZEdit, with its companion files together.\nKeep the Reconstruction.json companion: it preserves native details and existing\nrecords while their editor inputs are unchanged. Edited components are rebuilt.\nAuthor layers, filenames and editing history cannot be recovered.\n').encode()
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for filename,data in files.items():
            info=zipfile.ZipInfo(filename,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,data)
    return dict(files=len(files),source_sha256=hashlib.sha256(Path(resource).read_bytes()).hexdigest())


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--id',required=True);p.add_argument('--name',required=True);p.add_argument('--author',required=True)
    a=p.parse_args();print(json.dumps(reconstruct(a.source,a.output,a.id,a.name,a.author)))
