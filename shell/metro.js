(function () {
  const page = (location.pathname.split('/').pop() || 'm-home.html');
  const go = (p) => { location.href = p; };
  const q = (s) => document.querySelector(s);
  const qa = (s) => Array.from(document.querySelectorAll(s));

  if (page !== 'm-shade.html') {
    let pull = null;
    document.body.addEventListener('pointerdown', (e) => { if (e.clientY < 28) pull = e.clientY; });
    document.body.addEventListener('pointermove', (e) => {
      if (pull !== null && e.clientY - pull > 70) { pull = null; go('m-shade.html'); }
    });
    document.body.addEventListener('pointerup', () => { pull = null; });
  }

  if (page === 'm-player.html') {
    const play = q('#play');
    const playSVG = play ? play.innerHTML : '';
    let playing = false;
    if (play) play.addEventListener('click', () => {
      playing = !playing;
      play.innerHTML = playing
        ? '<svg class="ic" viewBox="0 0 38 38" fill="currentColor"><rect x="11" y="8" width="5" height="22"/><rect x="22" y="8" width="5" height="22"/></svg>'
        : playSVG;
    });
    qa('#shuffle, #repeat').forEach((b) => b.addEventListener('click', () => b.classList.toggle('on')));
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

  if (page === 'm-shade.html') {
    const close = () => go('m-home.html');
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
        if (a === 'player') return go('m-player.html');
        if (a === 'lock') return go('m-lock.html');
        if (a === 'power') return go('m-power.html');
        t.classList.toggle('on');
      });
    });
  }

  if (page === 'm-lock.html') {
    let y0 = null;
    const handle = q('.handle'), chev = q('.chev-up'), hint = q('.hint');
    document.body.addEventListener('pointerdown', (e) => { y0 = e.clientY; });
    document.body.addEventListener('pointermove', (e) => {
      if (y0 === null) return;
      const dy = Math.min(160, Math.max(0, y0 - e.clientY));
      if (handle) handle.style.top = (700 - dy / 3) + 'px';
      if (chev) chev.style.top = (674 - dy / 3) + 'px';
      if (dy >= 120 && hint) hint.textContent = 'отпустите';
    });
    document.body.addEventListener('pointerup', (e) => {
      if (y0 === null) return;
      const dy = y0 - e.clientY;
      y0 = null;
      if (dy >= 120) { go('m-home.html'); return; }
      if (handle) handle.style.top = '700px';
      if (chev) chev.style.top = '674px';
      if (hint) hint.textContent = 'проведите вверх';
    });
  }

  if (page === 'm-shsettings.html') {
    qa('.sw').forEach((s) => s.addEventListener('click', () => s.classList.toggle('on')));
    qa('.ch').forEach((c) => c.addEventListener('click', () => {
      const r = c.closest('.srow');
      if (r && r.dataset.go) go(r.dataset.go);
    }));
  }

  qa('[data-unpin]').forEach((u) => u.addEventListener('click', (e) => {
    e.preventDefault();
    e.stopPropagation();
    const t = u.closest('.tile');
    if (t) t.remove();
  }));
  qa('[data-pin]').forEach((pn) => pn.addEventListener('click', (e) => {
    e.preventDefault();
    e.stopPropagation();
    pn.classList.toggle('on');
  }));

  if (page === 'm-home.html') {
    let timer = null;
    qa('.tile').forEach((t) => {
      t.addEventListener('pointerdown', () => {
        timer = setTimeout(() => go('m-edit.html'), 650);
      });
      t.addEventListener('pointerup', () => { if (timer) clearTimeout(timer); });
      t.addEventListener('pointerleave', () => { if (timer) clearTimeout(timer); });
    });
  }
})();
