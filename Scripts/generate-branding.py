#!/usr/bin/env python3
"""Derive resources only from the approved PNG; never edit or crop the master."""
import argparse, io, pathlib
from PIL import Image
parser=argparse.ArgumentParser()
parser.add_argument('--root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
args=parser.parse_args();root=args.root.resolve()
with Image.open(root/'logo/xaraxtreme.png') as source:
    image=source.convert('RGBA')

def save(name,size,padding,background):
    available=(size[0]-2*padding,size[1]-2*padding)
    scale=min(available[0]/image.width,available[1]/image.height)
    fitted=image.resize((max(1,round(image.width*scale)),max(1,round(image.height*scale))),getattr(Image, "Resampling", Image).LANCZOS)
    canvas=Image.new('RGBA',size,background)
    canvas.alpha_composite(fitted,((size[0]-fitted.width)//2,(size[1]-fitted.height)//2))
    stream=io.BytesIO();canvas.save(stream,format='PNG',compress_level=9)
    path=root/'wxOil/xrc'/name;data=stream.getvalue()
    if not path.exists() or path.read_bytes()!=data:
        path.write_bytes(data)

save('vengeance-splash.png',(640,260),16,(15,15,18,255))
save('vengeance-about.png',(480,192),16,(15,15,18,255))
for size in [16,32,48,64,128,256]:
    save(f'vengeance-icon-{size}.png',(size,size),0,(0,0,0,0))
