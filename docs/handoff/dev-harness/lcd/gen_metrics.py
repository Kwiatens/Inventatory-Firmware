# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

from PIL import ImageFont
L='/usr/share/fonts/truetype/liberation/'
D='/usr/share/fonts/truetype/dejavu/'
specs=[(L+'LiberationSans-Regular.ttf',11),(L+'LiberationSans-Bold.ttf',11),(D+'DejaVuSansMono.ttf',12),(L+'LiberationSans-Bold.ttf',14)]
rows=[]
for p,s in specs:
    f=ImageFont.truetype(p,s)
    r=[]
    for c in range(128):
        if c<32: r.append(0)
        elif p.endswith('Mono.ttf'): r.append(7.0)
        else: r.append(round(f.getlength(chr(c))*0.93,2))   # helv bitmap fonts run a little narrower
    rows.append(r)
print('double g_char_w[4][128] = {')
for r in rows: print('{'+','.join(str(x) for x in r)+'},')
print('};')
