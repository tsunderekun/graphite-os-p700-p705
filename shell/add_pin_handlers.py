p = r'.\shell\metro.js'
s = open(p, encoding='utf-8').read()

add = r'''
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
'''

if 'data-unpin' not in s:
    idx = s.rindex('})();')
    s = s[:idx] + add + s[idx:]
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('pin/unpin js')
