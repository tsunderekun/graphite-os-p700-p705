# Medeo — шрифты graphite shell

Четыре static TTF из [Medeo](https://github.com/DNFDEVS/medeo-font) (ветка
`main`, версия **1.100**, upem 1000, не variable) с одним локальным патчем:
**табличные цифры (tnum)**.

| Файл | Роль в шелле | Вес | Размер |
|---|---|---|---|
| `MedeoSans-Regular.ttf` | тело, строки 15–16, значения | 400 | 238 KB |
| `MedeoSans-Bold.ttf` | Bold: заголовки строк, кнопки | 700 | 240 KB |
| `MedeoSansDisplay-ExtraLight.ttf` | заголовки 24–30, часы 76 (`TF_LIGHT`) | 200 | 236 KB |
| `MedeoMono-Regular.ttf` | терминал, моно 12 (`TF_MONO`) | 400 | 202 KB |

## Что пропатчено

Только cmap. В GSUB у Medeo уже есть фича `tnum` (`.tf`-альтернаты цифр), но
шелл грузит глифы через `FT_Load_Char` и OpenType-фичи не запускает. Патч
(исходник — `artifacts/fonts/patch_tnum.py`, вне git) переписывает cmap для
U+0030–U+0039 на эти альтернаты:

- Sans Regular/Bold: advance 656/1000 у всех десяти цифр;
- Display ExtraLight: 626/1000;
- Mono: цифры и так табличные (600/1000), cmap не менялся.

Без патча `00:00` = 44.9 px, а `11:11` = 25.0 px при 15 px — часы и счётчики
дёргаются. С патчем все строки времени равной ширины (проверка: см.
`notes/FONT-MEDEO.md`, §5 и §8.3).

`name`-таблицы (копирайт ID 0, лицензия ID 13/14) патч не трогает — файлы
остаются OFL-производными.

## Как пересобрать патч

```sh
python3 artifacts/fonts/patch_tnum.py MedeoSans-Regular.ttf MedeoSans-Regular.ttf
```

(для каждого из четырёх файлов; оригиналы — `artifacts/fonts/medeo/`).

## Лицензия

SIL Open Font License 1.1 — текст в `OFL.txt` рядом. Распространять вместе с
прошивкой/устройством и модифицировать можно; продавать шрифт отдельно и
перелицензировать — нельзя. Копирайты Medeo / Geist / Inter / ParaType / Noto
сохранены в `name`-таблицах.

## Установка

`os/install.sh` копирует этот каталог в `/usr/share/fonts/medeo`. Шелл ищет
Medeo-имена там, затем DejaVu в том же каталоге, затем
`/usr/share/fonts/dejavu` — без Medeo всё работает на DejaVu как раньше.
Отдельный каталог выбран специально: `xterm -fa 'DejaVu Sans Mono'`
резолвится через fontconfig, подменять файлы DejaVu нельзя.
