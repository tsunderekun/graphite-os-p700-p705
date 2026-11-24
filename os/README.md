# graphite shell — Xlib-оболочка LG P700

Один процесс, один полноэкранный X-окно 480×800, свой Canvas 565, вывод через
`XPutImage`. Без WM, без GTK/Qt, без шрифтовых движков кроме FreeType.

## Сборка и запуск

```sh
# на телефоне (Alpine, g++, libX11-dev, freetype-dev)
cd /home/user/os
./build.sh                 # -> build/graphite-shell
sudo ./run.sh              # запуск в текущем X-дисплее
```

> На 1 ГГц сборка при запущенном X уходит в своп: останавливайте X и
> собирайте `OPT=-O0 sh build.sh`, либо проверяйте экраны на хосте через
> `preview.sh`.
>
> После правки `include/graphite.h` (размер `App`/`ScreenView`, новые поля)
> нужен **полный** пересбор: `rm -f build/*.o && OPT=-O0 sh build.sh`.
> Инкрементальная сборка оставит старые объекты с другим ABI — экраны
> падают с `std::bad_alloc` или портят память.

### Загрузка в шелл

```sh
sudo cp build/graphite-shell /usr/local/bin/
sudo cp session-shell /usr/local/bin/ && sudo chmod +x /usr/local/bin/session-shell
sudo cp zz-mode.start /etc/local.d/ && sudo chmod +x /etc/local.d/zz-mode.start
echo shell | sudo tee /etc/lgtest-mode
```

`session-shell` поднимает X :0, `fbpan-ng 10`, запускает шелл и при падении
уходит в `lp7player` (аварийный режим). `zz-mode.start` выбирает сессию по
`/etc/lgtest-mode`: `shell`, `desktop` или `player`.


Синхронизация с ПК (`GRAPHITE_HOST` / `GRAPHITE_PASS` в окружении):

```sh
export GRAPHITE_HOST=<device-ip> GRAPHITE_PASS=<device-password>
python os/sync.py os /home/user/os
python os/ssh.py "cd /home/user/os && sh build.sh"
```

Проверка одного файла (так делают агенты, чтобы не мешать друг другу):

```sh
g++ -O2 -std=c++17 -Wall -Wextra -Iinclude -I/usr/include/freetype2 \
    -c screens/player.cpp -o /tmp/player.o
```

## Шрифты

В репозитории — четыре static TTF **Medeo** в `os/fonts/medeo/`, с одним
локальным патчем: **табличные цифры (tnum)**. Без него часы и счётчики
дёргаются по ширине (`00:00` 44.9 px против `11:11` 25.0 px при 15 px).

| Файл | Слот | Вес |
|---|---|---|
| `MedeoSans-Regular.ttf` | тело 15–16, значения | 400 |
| `MedeoSans-Bold.ttf` | `TF_BOLD`: заголовки строк, кнопки | 700 |
| `MedeoSansDisplay-ExtraLight.ttf` | `TF_LIGHT`: заголовки 24–30, часы 76 | 200 |
| `MedeoMono-Regular.ttf` | `TF_MONO`: терминал, моно 12 | 400 |

`install.sh` кладёт их в `/usr/share/fonts/medeo` (отдельный каталог:
`DejaVuSans*` не подменяем — `xterm -fa 'DejaVu Sans Mono'` резолвится через
fontconfig).

Порядок поиска в `gfx_init()`, по каждому слоту:

- без `GRAPHITE_FONTS` — `MedeoSans*` в `/usr/share/fonts/medeo`, затем
  `DejaVuSans*` там же, затем `DejaVuSans*` в `/usr/share/fonts/dejavu`;
- с `GRAPHITE_FONTS` или аргументом — этот каталог главный: Medeo-имена,
  затем DejaVu-имена в нём, затем DejaVu в `/usr/share/fonts/dejavu`
  (поэтому на устройстве можно насильно вернуться на DejaVu).

Нет Medeo — шелл полностью работает на DejaVu; нет отдельного веса —
`face_for()` падает на Regular.

Базлайн нормализуется в `Gfx::text`: если проектный `ascender/upem > 0.97`
(у Medeo 1.005 против 0.928 у DejaVu), ascent берётся как `ceil(px * 0.928)` —
ровно тот, что FreeType считает для DejaVu на любом кегле 11–40, так что Medeo
встаёт на базлайн DejaVu пиксель в пиксель. Для DejaVu ветка не срабатывает —
её вывод байт в байт прежний.

Переопределить каталог:

```sh
GRAPHITE_FONTS=/usr/share/fonts/medeo ./build/graphite-shell
GRAPHITE_SCREEN=lock GRAPHITE_FONTS=./os/fonts/medeo \
    sh os/preview.sh medeo-lock :91
```

Лицензия — **SIL OFL 1.1** (`os/fonts/medeo/OFL.txt`, копируется рядом со
шрифтами). Копирайты Medeo/Geist/Inter/ParaType/Noto сохранены в `name`;
патч cmap их не трогает. Детали и замеры — `notes/FONT-MEDEO.md`, устройство
каталога — `os/fonts/medeo/README.md`.

## Контракт экрана

```cpp
struct ScreenView {
    virtual const char *id() const = 0;
    virtual void draw(Gfx &g) = 0;              // весь экран, включая фон
    virtual bool tap(int x, int y);             // true = перерисовать
    virtual bool longtap(int x, int y);
    virtual bool drag(int x, int y, int dx, int dy);
    virtual bool key(int ksym, bool down);
    virtual void tick(unsigned now);
    virtual bool animated() const;              // tick каждые 100 мс
};
```

- Экран рисует **всё**, кроме статус-бара: первым делом `ui_bg(g)`.
- Статус-бар (32 px), тост и обработку Back/Home рисует/делает ядро.
- Переходы: `app.open("settings")` — **вглубь** (стек), `app.replace("queue")`
  — заменить текущую страницу (пивоты), `app.push("shade")` — оверлей,
  `app.back()` — назад; на корне — на «Пуск». `Home` всегда сбрасывает стек.
- Фабрика: экран объявляет `ScreenView *make_<id>()` в своём файле;
  `screens/stub.cpp` содержит weak-заглушки, поэтому реестр и заглушки
  править не нужно.
- Идентификаторы: `home player queue info apps edit shade lock power files
  terminal notes browser reader video set-root set-display set-wifi set-keys
  set-sound set-power set-storage set-apps set-access set-system set-lab
  set-about set-launch`.

## Клавиши

| Клавиша | evdev | keysym | действие |
|---|---|---|---|
| Back | event0 `KEY_BACK` 158 | `XF86Back` | назад по стеку |
| Menu | event0 `KEY_MENU` 139 | `XF86MenuKB` | лист «КОМАНДЫ» |
| Home | event0/event1 `KEY_HOME` 102 | `Home` | на Пуск; удержание — шторка |
| Search | event0 `KEY_SEARCH` 217 | `XF86Search` | список приложений |
| Vol±/Mute | event1/event5 | `XF86Audio*` | громкость MPD + OSD |
| Power | event5 `KEY_POWER` 116 | `XF86PowerOff` | лок; удержание — выключение |

`event0` — контроллер тачскрина (там же Back/Menu/Home/Search), `event1` —
`u0_keypad` (Home + Vol), `event5` — `7k_handset` (Power/End/Media). Для
event5 нужен `70-graphite-input.conf`; без него Power не доходит до X.

## Правила

- 480×800, координаты целые; тап-цель ≥44 px; текст ≥11 px.
- Прямые углы, без теней/градиентов/анимаций. Один акцент на экран.
- Цвета — только из `theme.*` (16 токенов), не хардкодить hex.
- Фон и текст: `ui_bg`, `ui_status` (ядро), `ui_header`, `ui_section`,
  `ui_row`, `ui_switch`, `ui_slider`, `ui_seg`, `ui_tile`, `icon`.
- Шрифты Medeo (`TF_LIGHT` — заголовки 24–30, обычный 15–16, подписи 11–12);
  без Medeo — DejaVu как раньше, см. «Шрифты».
- Никаких блокирующих вызовов в `draw`; сеть/файлы — в `tick` с кэшем.
- Память: экран живёт в кэше `App` до конца процесса, состояние — в полях.

## Внешние приложения (план)

Запускать только от `user` (uid 10000), не от root. Шелл работает root ради
sysfs/подсветки, но `fork` для GUI должен сделать `setgid(10000)` +
`setuid(10000)` и выставить `HOME=/home/user`,
`XDG_RUNTIME_DIR=/run/user/10000`, `DISPLAY=:0`. X-доступ — cookie или
`xhost +SI:localuser:user` (dev: `-ac`). `xterm` — первый кандидат, он же
настоящий терминал вместо встроенной консоли на 5 команд.

## Файлы

```
include/graphite.h   API (заморожен)
src/gfx.cpp          Canvas, FreeType, иконки
src/theme.cpp        7 тем × 5 акцентов
src/settings.cpp     /etc/lgtest-settings, атомарная запись
src/ui.cpp           общие элементы
src/app.cpp          окно, события, цикл, тост
src/registry.cpp     id → фабрика
screens/*.cpp        экраны (по одному файлу на экран)
```
