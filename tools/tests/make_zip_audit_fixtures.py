"""Exercise ZIP.cpp against Python's ZIP/deflate writer and compare every byte."""
import io
import random
import subprocess
import sys
import zipfile
from pathlib import Path

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
rng=random.Random(1999)
sizes=(0,1,1023,1024,1025,16383,16384,16385,32768,65535)
for level in (0,1,9):
    path=root/f'deflate-level-{level}.zip'
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=level) as z:
        z.comment=b'Classic ZIP compatibility check'
        z.writestr('folder/',b'')
        for n in sizes:
            z.writestr(f'compressible-{n}.mn3',(b'Descent3MASTERED!'*((n+15)//16))[:n])
            z.writestr(f'random-{n}.mn3',bytes(rng.randrange(256) for _ in range(n)))
    output=root/f'level-{level}'
    subprocess.run([str(exe),'unpack',str(path),str(output)],check=True)
    with zipfile.ZipFile(path) as z:
        for i,entry in enumerate(z.infolist()):
            assert (output/f'{i}.bin').read_bytes()==z.read(entry),entry.filename
    print(f'PASS Python reference comparison, level {level}',flush=True)

# Writing to a non-seekable output forces the ZIP data-descriptor flag.
class NonSeekable(io.BytesIO):
    def seekable(self): return False
    def seek(self,*args): raise io.UnsupportedOperation('not seekable')
stream=NonSeekable()
with zipfile.ZipFile(stream,'w',compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr('descriptor.mn3',b'data-descriptor payload'*1024)
path=root/'data-descriptor.zip'; path.write_bytes(stream.getvalue())
output=root/'descriptor'
subprocess.run([str(exe),'unpack',str(path),str(output)],check=True)
with zipfile.ZipFile(path) as z: assert (output/'0.bin').read_bytes()==z.read('descriptor.mn3')
print('PASS data descriptor ZIP',flush=True)
