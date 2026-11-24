(function () {
  const page = (location.pathname.split('/').pop() || 'home.html');
  const go = (p) => { location.href = p; };
  const q = (s) => document.querySelector(s);
  const qa = (s) => Array.from(document.querySelectorAll(s));

  // dock: 4 quick apps
  const dockTargets = ['player.html', 'files.html', 'terminal.html', 'browser.html'];
  qa('.dock a').forEach((a, i) => {
    a.addEventListener('click', (e) => { e.preventDefault(); go(dockTargets[i]); });
  });
  const dockActive = { 'terminal.html': 2, 'files.html': 1, 'browser.html': 3 };
  if (dockActive[page] !== undefined) {
    qa('.dock a').forEach((a, i) => a.classList.toggle('active', i === dockActive[page]));
  }

  // boot mode switch (shell vs fb player)
  qa('.bseg').forEach((s) => s.addEventListener('click', () => {
    qa('.bseg').forEach((x) => x.classList.remove('on'));
    s.classList.add('on');
    const h = q('#boot-hint');
    if (h) h.textContent = 'применится после перезагрузки';
  }));
  const reboot = q('#reboot');
  if (reboot) reboot.addEventListener('click', () => {
    const h = q('#boot-hint');
    if (h) h.textContent = 'перезагрузка…';
  });

  // pull down from the status bar to open the shade
  if (page !== 'shade.html') {
    let pull = null;
    document.body.addEventListener('pointerdown', (e) => { if (e.clientY < 32) pull = e.clientY; });
    document.body.addEventListener('pointermove', (e) => {
      if (pull !== null && e.clientY - pull > 80) { pull = null; go('shade.html'); }
    });
    document.body.addEventListener('pointerup', () => { pull = null; });
  }

  if (page === 'shade.html') {
    const close = () => go('home.html');
    const dim = q('.shade-dim');
    if (dim) dim.addEventListener('click', close);
    let y0 = null;
    document.body.addEventListener('pointerdown', (e) => { y0 = e.clientY; });
    document.body.addEventListener('pointerup', (e) => {
      if (y0 !== null && y0 - e.clientY > 80) close();
      y0 = null;
    });
    qa('.qtile').forEach((t) => {
      t.addEventListener('click', () => {
        const a = t.dataset.act;
        if (a === 'player') return go('player.html');
        if (a === 'lock') return go('lock.html');
        if (a === 'power') return go('power.html');
        t.classList.toggle('on');
      });
    });
    qa('.slider').forEach((s) => {
      const bar = s.querySelector('.bar');
      const fill = s.querySelector('.bar i');
      const knob = s.querySelector('.knob');
      const val = s.querySelector('.val');
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
    if (np) np.addEventListener('click', () => go('player.html'));
  }

  if (page === 'home.html') {
    const targets = ['player.html', 'files.html', 'terminal.html', 'browser.html', 'shsettings.html', 'notes.html'];
    qa('.tile').forEach((t, i) => t.addEventListener('click', (e) => { e.preventDefault(); go(targets[i]); }));
    const np = q('.nowplaying');
    if (np) np.addEventListener('click', () => go('player.html'));
    const npb = q('.np-btn');
    if (npb) npb.addEventListener('click', (e) => e.stopPropagation());
  }

  if (page === 'player.html') {
    const left = q('.pbtn.left'), right = q('.pbtn.right');
    if (left) left.addEventListener('click', () => go('queue.html'));
    if (right) right.addEventListener('click', () => go('info.html'));
    qa('.edge').forEach((b) => b.addEventListener('click', () => b.classList.toggle('on')));

    const play = q('.tr.play');
    const playSVG = play ? play.innerHTML : '';
    let playing = false;
    if (play) play.addEventListener('click', () => {
      playing = !playing;
      play.innerHTML = playing
        ? '<svg class="icon" viewBox="0 0 38 38" fill="currentColor" stroke="none"><rect x="11" y="8" width="5" height="22"/><rect x="22" y="8" width="5" height="22"/></svg>'
        : playSVG;
    });

    const prog = q('.prog'), knob = q('.knob'), times = q('.times');
    if (prog && knob && times) {
      const seek = (e) => {
        const r = prog.getBoundingClientRect();
        const x = Math.max(0, Math.min(r.width, e.clientX - r.left));
        prog.querySelector('i').style.width = (x / r.width * 100) + '%';
        knob.style.left = (48 + x - 7) + 'px';
        const t = Math.round(228 * x / r.width);
        times.firstElementChild.textContent = Math.floor(t / 60) + ':' + String(t % 60).padStart(2, '0');
      };
      prog.addEventListener('click', seek);
      prog.addEventListener('pointermove', (e) => { if (e.buttons & 1) seek(e); });
    }
  }

  if (page === 'queue.html') {
    const b = q('.sbtn.left'); if (b) b.addEventListener('click', () => go('player.html'));
    qa('.qrow').forEach((r) => r.addEventListener('click', () => go('player.html')));
  }

  if (page === 'info.html') {
    const b = q('.sbtn.left'), g = q('.sbtn.right');
    if (b) b.addEventListener('click', () => go('player.html'));
    if (g) g.addEventListener('click', () => go('psettings.html'));
  }

  if (page === 'psettings.html') {
    const b = q('.sbtn.left'); if (b) b.addEventListener('click', () => go('info.html'));
    qa('.set-row').forEach((r) => r.addEventListener('click', () => {
      qa('.set-row').forEach((x) => x.classList.remove('sel'));
      qa('.set-mark').forEach((m) => m.classList.remove('on'));
      r.classList.add('sel');
      r.querySelector('.set-mark').classList.add('on');
    }));
    qa('.seg').forEach((s) => s.addEventListener('click', () => {
      qa('.seg').forEach((x) => x.classList.remove('on'));
      s.classList.add('on');
    }));
    const sys = qa('.sys');
    if (sys[0]) sys[0].addEventListener('click', () => go('lock.html'));
    if (sys[1]) sys[1].addEventListener('click', () => go('power.html'));
  }

  if (page === 'terminal.html' || page === 'notes.html') {
    const x = q('.xbtn'); if (x) x.addEventListener('click', (e) => { e.preventDefault(); go('home.html'); });
  }
  if (page === 'files.html' || page === 'browser.html' || page === 'shsettings.html') {
    const b = q('.abtn'); if (b) b.addEventListener('click', (e) => { e.preventDefault(); go('home.html'); });
  }
  if (page === 'browser.html') {
    const r = q('.xbtn'); if (r) r.addEventListener('click', (e) => { e.preventDefault(); location.reload(); });
  }

  if (page === 'lock.html') {
    let y0 = null;
    const handle = q('.handle'), chev = q('.chev-up'), hint = q('.hint');
    document.body.addEventListener('pointerdown', (e) => { y0 = e.clientY; });
    document.body.addEventListener('pointermove', (e) => {
      if (y0 === null) return;
      const dy = Math.min(160, Math.max(0, y0 - e.clientY));
      handle.style.top = (700 - dy / 3) + 'px';
      if (chev) chev.style.top = (674 - dy / 3) + 'px';
      if (dy >= 120 && hint) hint.textContent = 'отпустите';
    });
    document.body.addEventListener('pointerup', (e) => {
      if (y0 === null) return;
      const dy = y0 - e.clientY;
      y0 = null;
      if (dy >= 120) { go('home.html'); return; }
      handle.style.top = '700px';
      if (chev) chev.style.top = '674px';
      if (hint) hint.textContent = 'проведите вверх';
    });
  }

  if (page === 'power.html') {
    const cancel = q('.btn:not(.primary)'), ok = q('.primary');
    if (cancel) cancel.addEventListener('click', () => go('home.html'));
    if (ok) ok.addEventListener('click', () => {
      const d = q('.dialog');
      d.innerHTML = '<h1>Выключение…</h1><p>Данные сохраняются. Экран сейчас погаснет.</p>';
    });
  }

  if (page === 'notes.html') {
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
})();
