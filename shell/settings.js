/* settings.js — theme tokens + mock interactions for the settings screens.
   Token values are the RGB565 values from artifacts/themes/palettes.json
   (tokens565), so the browser shows what the panel will show. Accent triples
   come from the accent matrix in notes/THEMES-PROPOSAL.md. No network. */
(function () {
  'use strict';

  var THEMES = {
    'graphite': {
      name: 'Графит', code: 'GRAPHITE', family: 'dark', fam: 'ТЁМНАЯ',
      bg: '#101419', bar: '#081010', surf: '#192021', surf2: '#3a414a',
      line: '#3a454a', line2: '#212429', art: '#080c08', fg: '#efebef',
      dim: '#8c969c', track: '#424d52', sel: '#192021', num: '#7b828c',
      note: '#5a656b'
    },
    'ember': {
      name: 'Уголь', code: 'EMBER', family: 'dark', fam: 'ТЁМНАЯ',
      bg: '#101410', bar: '#100c08', surf: '#211c19', surf2: '#423929',
      line: '#4a4131', line2: '#292419', art: '#080808', fg: '#f7ebe6',
      dim: '#9c9284', track: '#4a4131', sel: '#211c10', num: '#8c8273',
      note: '#6b5d4a'
    },
    'slate': {
      name: 'Сланец', code: 'SLATE', family: 'dark', fam: 'ТЁМНАЯ',
      bg: '#191c21', bar: '#101419', surf: '#212429', surf2: '#3a454a',
      line: '#3a414a', line2: '#212831', art: '#101010', fg: '#efefef',
      dim: '#949aa5', track: '#424d52', sel: '#192429', num: '#7b868c',
      note: '#636973'
    },
    'paper': {
      name: 'Бумага', code: 'PAPER', family: 'light', fam: 'СВЕТЛАЯ',
      bg: '#f7ebde', bar: '#e6dfce', surf: '#fff7ef', surf2: '#e6d7c5',
      line: '#ceba9c', line2: '#e6dbc5', art: '#e6dfce', fg: '#211c10',
      dim: '#63594a', track: '#cebea5', sel: '#efdfce', num: '#6b6152',
      note: '#948a6b'
    },
    'paper-n': {
      name: 'Бумага · нейтр.', code: 'PAPER-N', family: 'light', fam: 'СВЕТЛАЯ',
      bg: '#efeff7', bar: '#e6e7e6', surf: '#fffbff', surf2: '#dedfde',
      line: '#bdc2c5', line2: '#dee3e6', art: '#e6ebef', fg: '#101819',
      dim: '#5a5d63', track: '#c5c6c5', sel: '#e6ebef', num: '#63696b',
      note: '#8c9694'
    },
    'book': {
      name: 'Книга', code: 'BOOK', family: 'grey', fam: 'СЕРАЯ',
      bg: '#cec2b5', bar: '#bdb6a5', surf: '#d6cebd', surf2: '#b5aa9c',
      line: '#9c9284', line2: '#b5ae9c', art: '#bdb6a5', fg: '#191810',
      dim: '#4a4131', track: '#ada294', sel: '#ded2c5', num: '#524d42',
      note: '#7b7163'
    },
    'book-n': {
      name: 'Книга · нейтр.', code: 'BOOK-N', family: 'grey', fam: 'СЕРАЯ',
      bg: '#c5c6c5', bar: '#b5babd', surf: '#ced2d6', surf2: '#adaead',
      line: '#949a9c', line2: '#adb2b5', art: '#b5babd', fg: '#191c19',
      dim: '#42494a', track: '#a5a6a5', sel: '#d6d7d6', num: '#4a5152',
      note: '#73757b'
    }
  };

  /* accent → [base, press, on] per register; dark themes use `dark`,
     paper uses `ink`, book uses `deep` (THEMES-PROPOSAL §5). */
  var ACCENTS = {
    'turquoise': { name: 'Бирюза', dark: ['#3fc8d0', '#2fb6c0', '#0f1114'], ink: ['#0b7076', '#085c61', '#f7f2e7'], deep: ['#085357', '#064246', '#f2ece0'] },
    'amber':     { name: 'Янтарь', dark: ['#e0a052', '#c98b3f', '#171008'], ink: ['#8a5a12', '#6f470c', '#fbf4e6'], deep: ['#67430d', '#52360a', '#f2ece0'] },
    'blue':      { name: 'Синий',  dark: ['#6aa8e0', '#4f90c8', '#0a1116'], ink: ['#1f5e96', '#174a78', '#f2f7fb'], deep: ['#194d7b', '#143e62', '#f2ece0'] },
    'red':       { name: 'Красный', dark: ['#e4675c', '#c9544a', '#1a0b09'], ink: ['#a1362c', '#832a21', '#fbf1ee'], deep: ['#842c24', '#6a231d', '#f2ece0'] },
    'green':     { name: 'Зелёный', dark: ['#79c47a', '#61ad63', '#0b160c'], ink: ['#2f6b33', '#24532a', '#f2f8f1'], deep: ['#255328', '#1e4220', '#f2ece0'] }
  };

  var TEX_LABEL = { auto: 'АВТО', grain: 'ЗЕРНО', laid: 'ВЕРЖЕ', both: 'ЗЕРНО+ВЕРЖЕ', off: 'ВЫКЛ' };

  var qa = function (s) { return Array.prototype.slice.call(document.querySelectorAll(s)); };
  var params = new URLSearchParams(location.search);

  var state = {
    theme: THEMES[params.get('theme')] ? params.get('theme') : 'graphite',
    accent: ACCENTS[params.get('accent')] ? params.get('accent') : 'turquoise',
    tex: TEX_LABEL[params.get('tex')] ? params.get('tex') : 'auto',
    mode: params.get('boot') === 'nox' ? 'nox' : 'shell'
  };
  state.texLast = state.tex === 'off' ? 'auto' : state.tex;
  var MODE_LABEL = { shell: 'Оболочка', nox: 'Без X' };
  state.modeLabel = MODE_LABEL[state.mode];

  function register() {
    var f = THEMES[state.theme].family;
    return f === 'dark' ? 'dark' : (f === 'light' ? 'ink' : 'deep');
  }

  function accentTriple(id) {
    return ACCENTS[id][register()];
  }

  function textureFile() {
    var t = THEMES[state.theme];
    if (state.tex === 'off') return null;
    if (state.tex === 'laid') return 'tex/laid-' + state.theme + '.png';
    if (state.tex === 'grain') return t.family === 'dark' ? null : 'tex/grain-' + state.theme + '.png';
    if (state.tex === 'both') {
      return t.family === 'dark' ? 'tex/laid-' + state.theme + '.png'
                                 : 'tex/grain-laid-' + state.theme + '.png';
    }
    /* auto: paper/book grain, dark nothing */
    return t.family === 'dark' ? null : 'tex/grain-' + state.theme + '.png';
  }

  function setVar(k, v) { document.documentElement.style.setProperty('--' + k, v); }

  function paint() {
    var t = THEMES[state.theme];
    var a = accentTriple(state.accent);
    var keys = ['bg', 'bar', 'surf', 'surf2', 'line', 'line2', 'art', 'fg',
                'dim', 'track', 'sel', 'num', 'note'];
    keys.forEach(function (k) { setVar(k, t[k]); });
    setVar('accent', a[0]);
    setVar('accent2', a[1]);
    setVar('onacc', a[2]);

    var file = textureFile();
    var texCss = file ? 'url("' + file + '")' : 'none';
    document.documentElement.style.setProperty('--tex', texCss);
    document.body.style.backgroundImage = texCss;
    document.body.dataset.theme = state.theme;
    document.body.dataset.family = t.family;
    document.body.dataset.accent = state.accent;
    document.title = document.title.replace(/\s·\s[^·]+$/, '') +
      ' · ' + t.name + ' / ' + ACCENTS[state.accent].name;

    qa('[data-theme-name]').forEach(function (e) { e.textContent = t.name; });
    qa('[data-theme-code]').forEach(function (e) { e.textContent = t.code; });
    qa('[data-accent-name]').forEach(function (e) { e.textContent = ACCENTS[state.accent].name; });
    var uiTex = state.tex === 'auto' ? 'auto' : (state.tex === 'off' ? 'off' : 'on');
    qa('[data-tex-name]').forEach(function (e) {
      e.textContent = uiTex === 'auto' ? 'АВТО' : (uiTex === 'on' ? 'ВКЛ' : 'ВЫКЛ');
    });

    qa('.trow').forEach(function (e) {
      e.classList.toggle('on', e.dataset.setTheme === state.theme);
    });
    qa('[data-theme-swatch]').forEach(function (s) {
      var tt = THEMES[s.dataset.themeSwatch];
      s.firstElementChild.style.background = tt.bg;
      s.lastElementChild.style.background = tt.surf;
      s.style.outlineColor = tt.line;
    });

    qa('.acc').forEach(function (e) {
      var id = e.dataset.setAccent;
      e.classList.toggle('on', id === state.accent);
      var col = ACCENTS[id][register()][0];
      var dot = e.querySelector('.dot');
      if (dot) dot.style.background = col;
    });

    qa('[data-tex]').forEach(function (e) {
      e.classList.toggle('on', e.dataset.tex === uiTex);
      e.classList.remove('dis');
      e.removeAttribute('aria-disabled');
    });

    var texOff = state.tex === 'off';
    qa('[data-tex-toggle]').forEach(function (row) {
      row.setAttribute('aria-checked', texOff ? 'false' : 'true');
      var sw = row.querySelector('.sw');
      if (sw) {
        sw.classList.toggle('on', !texOff);
        sw.setAttribute('aria-checked', texOff ? 'false' : 'true');
      }
    });
    var sub = document.querySelector('[data-tex-sub]');
    if (sub) {
      sub.textContent = texOff ? 'выключена · ровный фон'
        : (state.tex === 'auto' ? 'шероховатость · авто'
                                : 'шероховатость · включена');
    }
    var note = document.querySelector('[data-tex-note]');
    if (note) {
      note.textContent = texOff
        ? 'Текстура выключена везде; фон остаётся цветом темы'
        : (state.tex === 'auto'
            ? 'АВТО: зерно на бумаге и книге; на тёмных зерна нет, только верже'
            : 'Зерно на светлых темах, верже на тёмных; выключите, если не нравится');
    }

    qa('[data-boot]').forEach(function (e) {
      e.classList.toggle('on', e.dataset.boot === state.mode);
    });
    qa('[data-mode-name]').forEach(function (e) { e.textContent = state.modeLabel; });
    var codes = { shell: 'shell', nox: 'no-X' };
    qa('[data-mode-code]').forEach(function (e) { e.textContent = codes[state.mode] || state.mode; });
  }

  function queryString() {
    var p = new URLSearchParams();
    p.set('theme', state.theme);
    p.set('accent', state.accent);
    p.set('tex', state.tex);
    p.set('boot', state.mode);
    return '?' + p.toString();
  }

  function syncUrl() {
    try { history.replaceState(null, '', location.pathname + queryString()); } catch (e) { /* file:// */ }
    qa('a[data-back]').forEach(function (a) { a.href = 's-root.html' + queryString(); });
    qa('[data-go]').forEach(function (a) {
      var target = a.dataset.go;
      if (target && target.indexOf('.html') >= 0) a.href = target + queryString();
    });
  }

  function normaliseTex() {
    var fam = THEMES[state.theme].family;
    if (fam === 'dark' && (state.tex === 'grain' || state.tex === 'both')) {
      state.tex = 'laid';
    }
  }

  function update() {
    normaliseTex();
    paint();
    syncUrl();
  }

  /* --- interactions ----------------------------------------------------- */
  document.addEventListener('click', function (ev) {
    var t = ev.target;

    var texRow = t.closest ? t.closest('[data-tex-toggle]') : null;
    if (texRow) {
      ev.stopPropagation();
      ev.preventDefault();
      if (state.tex === 'off') {
        state.tex = state.texLast || 'auto';
      } else {
        state.texLast = state.tex;
        state.tex = 'off';
      }
      update();
      return;
    }

    var sw = t.closest ? t.closest('.sw') : null;
    if (sw) {
      ev.stopPropagation();
      ev.preventDefault();
      if (!sw.classList.contains('dis')) {
        sw.classList.toggle('on');
        sw.setAttribute('aria-checked', sw.classList.contains('on') ? 'true' : 'false');
      }
      return;
    }

    var cb = t.closest ? t.closest('.cb') : null;
    if (cb) {
      ev.stopPropagation();
      ev.preventDefault();
      cb.classList.toggle('on');
      return;
    }

    var pin = t.closest ? t.closest('[data-pin]') : null;
    if (pin) {
      ev.stopPropagation();
      ev.preventDefault();
      pin.classList.toggle('on');
      pin.setAttribute('aria-label', pin.classList.contains('on') ? 'Закреплено' : 'Не закреплено');
      return;
    }

    var theme = t.closest ? t.closest('[data-set-theme]') : null;
    if (theme) {
      state.theme = theme.dataset.setTheme;
      update();
      return;
    }

    var acc = t.closest ? t.closest('[data-set-accent]') : null;
    if (acc) {
      state.accent = acc.dataset.setAccent;
      update();
      return;
    }

    var tex = t.closest ? t.closest('[data-tex]') : null;
    if (tex) {
      var key = tex.dataset.tex;
      if (key === 'auto') state.tex = 'auto';
      else if (key === 'off') state.tex = 'off';
      else state.tex = THEMES[state.theme].family === 'dark' ? 'laid' : 'both';
      update();
      return;
    }

    var boot = t.closest ? t.closest('[data-boot]') : null;
    if (boot) {
      state.mode = boot.dataset.boot;
      state.modeLabel = MODE_LABEL[state.mode] || boot.textContent.trim();
      paint();
      syncUrl();
      return;
    }

    var seg = t.closest ? t.closest('.seg[data-seg]') : null;
    if (seg && t.closest('button')) {
      var b = t.closest('button');
      if (b.classList.contains('dis')) return;
      seg.querySelectorAll('button').forEach(function (x) { x.classList.remove('on'); });
      b.classList.add('on');
      var out = seg.getAttribute('data-out');
      if (out) {
        qa('[data-out="' + out + '"]').forEach(function (e) { e.textContent = b.textContent.trim(); });
      }
      return;
    }

    var opt = t.closest ? t.closest('.opt[data-launch-mode]') : null;
    if (opt) {
      state.mode = opt.dataset.launchMode;
      state.modeLabel = opt.querySelector('.nm').textContent.trim();
      paint();
      return;
    }

    var go = t.closest ? t.closest('[data-go]') : null;
    if (go && go.tagName === 'A' && go.getAttribute('href')) {
      /* normal navigation */
    }
  });

  /* sliders (pointer) */
  function initSlider(el) {
    var bar = el.querySelector('.bar');
    var fill = el.querySelector('.bar i');
    var knob = el.querySelector('.knob');
    var val = el.querySelector('.val');
    if (!bar || !fill || !knob || !val) return;
    var pct = Math.max(0, Math.min(100, parseInt(el.dataset.pct || '60', 10)));
    var isDb = el.dataset.db !== undefined;

    function render(p) {
      pct = Math.max(0, Math.min(100, p));
      fill.style.width = pct + '%';
      var w = bar.getBoundingClientRect().width;
      knob.style.left = (bar.getBoundingClientRect().left - el.getBoundingClientRect().left +
                         w * pct / 100 - 7) + 'px';
      if (isDb) {
        var step = Math.round(pct / 100 * 30);
        var db = Math.round(-40 + pct / 100 * 40);
        val.textContent = (db === 0 ? '0' : (db < 0 ? '−' + Math.abs(db) : db)) +
                          ' дБ · ' + step + '/30';
      } else {
        val.textContent = Math.round(pct) + ' %';
      }
    }
    function fromEvent(e) {
      var r = bar.getBoundingClientRect();
      render((e.clientX - r.left) / r.width * 100);
    }
    el.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      fromEvent(e);
    });
    el.addEventListener('pointermove', function (e) {
      if (el.hasPointerCapture && el.hasPointerCapture(e.pointerId)) fromEvent(e);
    });
    render(pct);
    window.addEventListener('resize', function () { render(pct); });
  }
  qa('.sld').forEach(initSlider);

  update();
})();
