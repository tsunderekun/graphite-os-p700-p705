p = r'.\shell\metro.css'
s = open(p, encoding='utf-8').read()

block = '''
button { border: 0; background: none; color: inherit; font: inherit; padding: 0; }
.apphead { position: relative; height: 56px; padding: 0 20px; display: flex; flex-direction: column; justify-content: center; }
.apphead h1 { margin: 0; font-size: 24px; font-weight: 200; }
.apphead .sub { margin-top: 2px; font-size: 11px; letter-spacing: 2px; color: #8e95a2; text-transform: uppercase; }
.apphead .back { position: absolute; left: -4px; top: 8px; width: 40px; height: 40px; display: flex; align-items: center; justify-content: center; }
.apphead.withback { padding-left: 44px; }
.swrow { position: relative; height: 56px; padding: 0 20px; border-top: 1px solid #22262b; }
.swrow .n { position: absolute; left: 20px; top: 8px; font-size: 16px; }
.swrow .v { position: absolute; left: 20px; top: 28px; font-size: 12px; color: #8e95a2; }
.notearea2 { position: absolute; left: 0; right: 0; top: 88px; height: 424px; padding: 16px 20px; font-size: 15px; line-height: 24px; }
.notearea2 h1 { font-size: 20px; font-weight: 200; margin: 0 0 12px; }
'''

if '.apphead {' not in s:
    s = s.rstrip() + '\n' + block
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('css block added')
else:
    print('already present')
