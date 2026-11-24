p = r'.\shell\notes.html'
s = open(p, encoding='utf-8').read()
a = s.index('  <div class="kb">')
b = s.index('  <nav class="dock">')
s = s[:a] + '  <div class="kb" id="kb"></div>\n\n' + s[b:]
open(p, 'w', encoding='utf-8', newline='\n').write(s)

p = r'.\shell\shell.js'
s = open(p, encoding='utf-8').read()
a = s.index("  if (page === 'notes.html') {")
b = s.rindex('})();')
new = r'''  if (page === 'notes.html') {
    const note = q('.notearea > div');
    const kb = document.getElementById('kb');
    let shift = false;
    if (note) {
      note.setAttribute('contenteditable', 'true');
      note.focus();
      const r = document.createRange();
      r.selectNodeContents(note);
      r.collapse(false);
      const sel = getSelection();
      sel.removeAllRanges();
      sel.addRange(r);
    }
    const layouts = {
      ru: [
        ['й','ц','у','к','е','н','г','ш','щ','з','х'],
        ['ф','ы','в','а','п','р','о','л','д','ж','э'],
        ['shift','я','ч','с','м','и','т','ь','б','ю','ъ'],
        ['123','lang','space','del','enter']
      ],
      en: [
        ['q','w','e','r','t','y','u','i','o','p'],
        ['a','s','d','f','g','h','j','k','l'],
        ['shift','z','x','c','v','b','n','m','del'],
        ['123','lang','space','enter']
      ]
    };
    let lang = 'ru';
    const caps = { shift: 'SHIFT', del: 'DEL', space: 'ПРОБЕЛ', enter: 'ВВОД', '123': '123' };
    function label(k) {
      if (k === 'lang') return lang === 'ru' ? 'ENG' : 'РУС';
      if (caps[k]) return caps[k];
      return shift ? k.toUpperCase() : k;
    }
    function press(k) {
      if (k === 'shift') { shift = !shift; render(); return; }
      if (k === 'lang') { lang = lang === 'ru' ? 'en' : 'ru'; shift = false; render(); return; }
      if (k === '123') return;
      if (note) note.focus();
      if (k === 'del') { document.execCommand('delete'); return; }
      if (k === 'space') { document.execCommand('insertText', false, ' '); return; }
      if (k === 'enter') { document.execCommand('insertParagraph'); return; }
      document.execCommand('insertText', false, shift ? k.toUpperCase() : k);
      if (shift) { shift = false; render(); }
    }
    function render() {
      if (!kb) return;
      kb.innerHTML = '';
      layouts[lang].forEach((row) => {
        const r = document.createElement('div');
        r.className = 'krow';
        row.forEach((k) => {
          const el = document.createElement('div');
          el.className = 'key';
          if (['shift', 'del', 'lang', '123'].includes(k)) el.classList.add('fn');
          if (k === 'shift' && shift) el.classList.add('on');
          if (k === 'space') el.style.flex = '4';
          if (k === 'enter') el.classList.add('enter');
          el.textContent = label(k);
          el.addEventListener('click', () => press(k));
          r.appendChild(el);
        });
        kb.appendChild(r);
      });
    }
    render();
  }
'''
s = s[:a] + new + s[b:]
open(p, 'w', encoding='utf-8', newline='\n').write(s)

p = r'.\shell\app.css'
s = open(p, encoding='utf-8').read()
if '.key.on' not in s:
    s += '\n.key.on { border-color: #3fc8d0; color: #3fc8d0; }\n'
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('done')
