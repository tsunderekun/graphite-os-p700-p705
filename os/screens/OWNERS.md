# Владельцы экранов

Один экран — один файл и одна фабрика `ScreenView *make_<id>()`. Слабые
заглушки в `stub.cpp` перекрываются сильным символом при линковке, поэтому
реестр и заглушки править не нужно.

| id | файл | кто | состояние |
|---|---|---|---|
| `home` | `home.cpp` | ядро | по пинам из `~/.config/graphite/pins` |
| `apps` | `apps.cpp` | launcher | список, ＋/✓ |
| `edit` | `edit.cpp` | launcher | сетка, ×, добавить |
| `set-root` | `set_root.cpp` | settings A | 11 строк |
| `set-display` | `set_display.cpp` | settings A | темы/акценты/текстура/яркость |
| `set-wifi` | `set_wifi.cpp` | settings A | Wi-Fi/RNDIS + серые |
| `set-sound` | `set_sound.cpp` | settings A | громкость/маршрут/сон |
| `set-power` | `set_power.cpp` | settings A | батарея/экономия |
| `set-keys` | `set_keys.cpp` | settings B | подсветка/коды |
| `set-storage` | `set_storage.cpp` | settings B | statvfs/кэш |
| `set-apps` | `set_apps.cpp` | settings B | `.desktop` |
| `set-access` | `set_access.cpp` | settings B | SSH/PIN |
| `set-system` | `set_system.cpp` | settings B | время/NTP |
| `set-lab` | `set_lab.cpp` | settings B | диагностика |
| `set-about` | `set_about.cpp` | settings B | сведения |
| `set-launch` | `set_launch.cpp` | settings B | shell/no-X |
| `player` | `player.cpp` | player | MPD, seek, vol |
| `queue` | `queue.cpp` | player | список |
| `info` | `info.cpp` | player | метаданные |
| `shade` | `shade.cpp` | chrome | overlay, жест сверху |
| `lock` | `lock.cpp` | chrome | часы, свайп |
| `power` | `power.cpp` | chrome | подтверждение |
| `files` | `files.cpp` | apps | каталоги |
| `terminal` | `terminal.cpp` | apps | 5 команд |
| `notes` | `notes.cpp` | apps | ЙЦУКЕН |
| `browser` | `browser.cpp` | apps | офлайн |
| `reader` | — | — | план `notes/READER-PLAN.md` |
| `video` | — | — | план `notes/VIDEO-PLAN.md` |

## Общее ядро (менять только с интеграцией)

`include/graphite.h`, `src/*.cpp`, `screens/stub.cpp`, `src/registry.cpp`.
Пины: `include/pins.h`, определения в `apps.cpp` (общие для home/apps/edit).
MPD: `include/mpd.h`, `src/mpd.cpp`.
