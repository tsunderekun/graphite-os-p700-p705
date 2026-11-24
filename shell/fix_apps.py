p = r'.\shell\m-apps.html'
s = open(p, encoding='utf-8').read()

old_pin = 'M12 3l6 6-2.5 1.5L12 14l-1 4-2 2-1.5-1.5 2-2 4-1 3.5-3.5L18 9l-6-6z" fill="none"'
new_pin = 'M12 5v14M5 12h14" fill="none"'
s = s.replace(old_pin, new_pin)

hint = '  <div class="path" style="left:20px;top:772px">＋ — на главный экран · ✓ — убрать</div>\n'
s = s.replace('  <script src="metro.js"></script>', hint + '  <script src="metro.js"></script>')

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('apps updated')
