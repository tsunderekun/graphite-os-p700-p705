p = r'.\shell\metro.js'
s = open(p, encoding='utf-8').read()

add = r'''
  qa('.sw').forEach((s) => s.addEventListener('click', (e) => { e.stopPropagation(); s.classList.toggle('on'); }));
  qa('[data-go]').forEach((r) => r.addEventListener('click', () => go(r.dataset.go)));

  if (page === 'm-shade.html') {
    const close = () => go('m-home.html');
    const dim = q('.shade-dim');
    if (dim) dim.addEventListener('click', close);
    let ys = null;
    document.body.addEventListener('pointerdown', (e) => { ys = e.clientY; });
    document.body.addEventListener('pointerup', (e) => { if (ys !== null && ys - e.clientY > 80) close(); ys = null; });
    qa('.qtile').forEach((t) => t.addEventListener('click', () => {
      const a = t.dataset.act;
      if (a === 'player') return go('m-player.html');
      if (a === 'lock') return go('m-lock.html');
      if (a === 'power') return go('m-power.html');
      t.classList.toggle('on');
    }));
    qa('.slider').forEach((sl) => {
      const bar = sl.querySelector('.bar'), fill = sl.querySelector('.bar i');
      const knob = sl.querySelector('.knob'), val = sl.querySelector('.val');
      const set = (e) => {
        const r = bar.getBoundingClientRect();
        const x = Math.max(0, Math.min(r.width, e.clientX - r.left));
        const pct = Math.round(x / r.width * 100);
        fill.style.width = pct + '%';
        knob.style.left = (x - 7) + 'px';
        val.textContent = pct + '%';
      };
      bar.addEventListener('click', set);
      bar.addEventListener('pointermove', (e) => { if (e.buttons & 1) set(e); });
    });
    const np = q('.notif[data-act="np"]');
    if (np) np.addEventListener('click', () => go('m-player.html'));
  }

  if (page === 'm-power.html') {
    const off = q('#off');
    if (off) off.addEventListener('click', (e) => {
      e.preventDefault();
      const d = q('.dialog');
      d.innerHTML = '<h1>Выключение…</h1><p>Данные сохраняются. Экран сейчас погаснет.</p>';
    });
  }

  if (page === 'm-notes.html') {
    const note = q('.notearea2 > div');
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
    qa('.key').forEach((k) => k.addEventListener('click', () => {
      const t = k.textContent.trim();
      if (t === 'SHIFT') { shift = !shift; k.classList.toggle('on'); return; }
      if (t === 'ENG' || t === '123') return;
      if (note) note.focus();
      if (t === 'DEL') { document.execCommand('delete'); return; }
      if (t === 'ПРОБЕЛ') { document.execCommand('insertText', false, ' '); return; }
      if (t === 'ВВОД') { document.execCommand('insertParagraph'); return; }
      document.execCommand('insertText', false, shift ? t.toUpperCase() : t);
      if (shift) { shift = false; qa('.key').forEach((x) => x.classList.remove('on')); }
    }));
  }
'''

if 'm-shade.html' not in s:
    idx = s.rindex('})();')
    s = s[:idx] + add + s[idx:]
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('handlers added')
