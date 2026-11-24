import glob

tag = '  <script src="shell.js"></script>\n</body>'
for p in glob.glob(r'.\shell\*.html'):
    s = open(p, encoding='utf-8').read()
    if 'shell.js' not in s:
        s = s.replace('</body>', tag)
        open(p, 'w', encoding='utf-8', newline='\n').write(s)
        print('tag:', p.split('\\')[-1])

p = r'.\shell\style.css'
s = open(p, encoding='utf-8').read()
if 'cursor: pointer' not in s:
    s += '\n.tile, .dock a, .pbtn, .np-btn, .nowplaying, .qrow, .sbtn, .edge, .tr, .key, .seg, .sys, .set-row, .frow, .abtn, .xbtn, .btn { cursor: pointer; }\n'
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('cursor css added')
