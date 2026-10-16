# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

import sys
from PIL import Image, ImageDraw, ImageFont
L='/usr/share/fonts/truetype/liberation/'; D='/usr/share/fonts/truetype/dejavu/'
FONTS=[ImageFont.truetype(L+'LiberationSans-Regular.ttf',11),ImageFont.truetype(L+'LiberationSans-Bold.ttf',11),
       ImageFont.truetype(D+'DejaVuSansMono.ttf',14),ImageFont.truetype(L+'LiberationSans-Bold.ttf',14)]
W,H=128,64
def frames(lines):
    px=None; color=1; out=[]
    def put(x,y,c=None):
        if 0<=x<W and 0<=y<H:
            c=color if c is None else c
            px[y][x]= (1 if c==1 else 0) if c!=2 else 1-px[y][x]
    def fill(x,y,w,h):
        for yy in range(y,y+h):
            for xx in range(x,x+w): put(xx,yy)
    for ln in lines:
        t=ln.rstrip('\n').split(' ')
        k=t[0]
        if k=='clear': px=[[0]*W for _ in range(H)]; color=1
        elif k=='color': color=int(t[1])
        elif k in('box','rbox'): fill(*map(int,t[1:5]))
        elif k=='frame':
            x,y,w,h=map(int,t[1:5])
            fill(x,y,w,1); fill(x,y+h-1,w,1); fill(x,y,1,h); fill(x+w-1,y,1,h)
        elif k=='tri':
            im=Image.new('1',(W,H),0); ImageDraw.Draw(im).polygon([tuple(map(int,t[1:3])),tuple(map(int,t[3:5])),tuple(map(int,t[5:7]))],outline=1,fill=0)
            for yy in range(H):
                for xx in range(W):
                    if im.getpixel((xx,yy)): put(xx,yy)
        elif k=='xbm':
            x,y,w,h=map(int,t[1:5])
            bpr=(w+7)//8; data=list(map(int,t[5:5+h*bpr]))
            for r in range(h):
                for c in range(w):
                    if data[r*bpr+c//8]>>(c%8)&1: put(x+c,y+r)
        elif k=='str':
            x,y,f=int(t[1]),int(t[2]),int(t[3]); s=' '.join(t[4:])
            im=Image.new('1',(W+40,H+20),0); d=ImageDraw.Draw(im); d.fontmode='1'
            if f==2:
                for i,ch in enumerate(s): d.text((i*7,y+10),ch,font=FONTS[2],fill=1,anchor='ls')
            else: d.text((0,y+10),s,font=FONTS[f],fill=1,anchor='ls')
            for yy in range(H):
                for xx in range(W+40):
                    if im.getpixel((xx,yy+10)) and 0<=x+xx<W: put(x+xx,yy)
        elif k=='send':
            out.append([r[:] for r in px])
    return out
def to_img(px,s=4,border=True):
    im=Image.new('RGB',(W*s,H*s),(190,205,170))
    d=ImageDraw.Draw(im)
    for y in range(H):
        for x in range(W):
            if px[y][x]: d.rectangle([x*s,y*s,x*s+s-1,y*s+s-1],fill=(20,28,20))
    return im
if __name__=='__main__':
    raw=open(sys.argv[1]).read().split('\n')
    out=sys.argv[2]; cols=int(sys.argv[3]) if len(sys.argv)>3 else 3
    groups=[];cur=None
    for ln in raw:
        if ln.startswith('screen '): cur=[ln[7:],[]]; groups.append(cur)
        elif cur: cur[1].append(ln)
    ims=[];labels=[]
    for name,lns in groups:
        fr=frames(lns)
        if not fr: continue
        if name.startswith('*'):
            for i,f in enumerate(fr): ims.append(to_img(f,3)); labels.append(f'{name[1:]} #{i}')
        else: ims.append(to_img(fr[-1],3)); labels.append(name)
    cw,ch=W*3+8,H*3+22
    rows=(len(ims)+cols-1)//cols
    sheet=Image.new('RGB',(cols*cw,rows*ch),(60,60,60)); d=ImageDraw.Draw(sheet)
    for i,(im,l) in enumerate(zip(ims,labels)):
        x,y=(i%cols)*cw,(i//cols)*ch
        sheet.paste(im,(x+4,y+18)); d.text((x+4,y+3),l,fill=(255,255,255))
    sheet.save(out)
