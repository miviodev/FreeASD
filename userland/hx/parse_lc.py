import struct, sys
path = sys.argv[1] if len(sys.argv) > 1 else 'build/hx'
data = open(path,'rb').read()
magic,cputype,cpusubtype,filetype,ncmds,sizeofcmds,flags,reserved = struct.unpack_from('<IIIIIIII', data, 0)
print(f'magic=0x{magic:08X} filetype={filetype} ncmds={ncmds}')
off = 32
for i in range(ncmds):
    cmd,cmdsize = struct.unpack_from('<II', data, off)
    s = f'  [{i}] cmd=0x{cmd:08X} size={cmdsize}'
    if cmd == 0x19:
        segname = data[off+8:off+24].rstrip(b'\x00').decode(errors='replace')
        vmaddr,vmsize,fileoff,filesize = struct.unpack_from('<QQQQ', data, off+24)
        initprot = struct.unpack_from('<I', data, off+56)[0]
        s += f' SEG={segname} vmaddr=0x{vmaddr:X} vmsize=0x{vmsize:X} fp={fileoff} initprot={initprot}'
    elif cmd == 0x80000028:
        entryoff,stacksize = struct.unpack_from('<QQ', data, off+8)
        s += f' LC_MAIN entryoff=0x{entryoff:X} stack=0x{stacksize:X}'
    elif cmd == 0x05:
        s += ' LC_UNIXTHREAD'
    print(s)
    off += cmdsize
