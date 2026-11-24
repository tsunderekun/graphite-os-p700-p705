from PIL import Image, ImageDraw, ImageFont

items = [
    ("m-home.png", "ПУСК"),
    ("m-player.png", "СЕЙЧАС"),
    ("m-queue.png", "ОЧЕРЕДЬ"),
    ("m-info.png", "ИНФО"),
    ("m-settings.png", "НАСТРОЙКИ ПЛЕЕРА"),
    ("m-apps.png", "ПРИЛОЖЕНИЯ +/✓"),
    ("m-edit.png", "ПРАВКА ПЛИТОК"),
    ("m-shsettings.png", "НАСТРОЙКИ УСТРОЙСТВА"),
    ("m-terminal.png", "ТЕРМИНАЛ"),
    ("m-notes.png", "ЗАМЕТКИ"),
    ("m-files.png", "ФАЙЛЫ"),
    ("m-browser.png", "БРАУЗЕР"),
    ("m-shade.png", "ШТОРКА"),
    ("m-lock.png", "БЛОКИРОВКА"),
    ("m-power.png", "ВЫКЛЮЧЕНИЕ"),
]

tw, th = 220, 367
pad = 20
label_h = 24
cols = 5
rows = (len(items) + cols - 1) // cols
W = cols * (tw + pad) + pad
H = rows * (th + label_h + pad) + pad + 44
sheet = Image.new("RGB", (W, H), (12, 14, 16))
d = ImageDraw.Draw(sheet)

f_title = ImageFont.truetype(r".\shell\fonts\DejaVuSans-ExtraLight.ttf", 26)
f_cap = ImageFont.truetype(r".\shell\fonts\DejaVuSans.ttf", 13)
d.text((pad, 8), "graphite shell — Metro, 15 экранов 480×800", fill=(232, 235, 238), font=f_title)

for i, (fn, cap) in enumerate(items):
    col, row = i % cols, i // cols
    x = pad + col * (tw + pad)
    y = 44 + row * (th + label_h + pad)
    im = Image.open(r".\artifacts\shell" + "\\" + fn).convert("RGB").resize((tw, th))
    sheet.paste(im, (x, y))
    d.text((x, y + th + 5), cap, fill=(142, 149, 162), font=f_cap)

sheet.save(r".\artifacts\shell\metro-sheet.png")
print("sheet", sheet.size)
