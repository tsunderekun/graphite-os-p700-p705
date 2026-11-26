[English versions](../en/index.md) · **Русский**

# Документация — технические планы

Срез 26.11.2026: shell работает на тестовом P705. Реализованы два слота задач,
шторка уведомлений, PIN-блокировка, Reader TXT/FB2/FB2.zip/EPUB2/3,
просмотрщик PNG/JPEG и часы/таймер. Часть новых экранов проверена только на
хосте; детали и ограничения указаны на страницах ниже. Это не релизный образ.

Русские оригиналы планов и разборов по проекту graphite shell (LG P700).
Тексты писались для себя, в рабочем порядке: это не отполированная
документация, а инженерные заметки с числами и решениями.

Английские версии — `docs/en/`; ключевые статусные сведения синхронизированы.

## Карта

| файл | о чём |
|---|---|
| [architecture.md](architecture.md) | архитектура шелла: X-сессия, окна, экраны, клавиатура, внешние приложения |
| [install.md](install.md) | установка postmarketOS: ядро, boot image, p9/p20, первый запуск, восстановление |
| [design-system.md](design-system.md) | токены, сетка 480×800, компоненты, состояния |
| [screens.md](screens.md) | 15 экранов Metro с размерами и поведением |
| [metro.md](metro.md) | что взято из Windows Phone 7/8 и что нет |
| [themes.md](themes.md) | 7 тем, 5 акцентов, текстуры под RGB565 |
| [settings-spec.md](settings-spec.md) | настройки: 11 групп Android → наши, 26 экранов, 35 ключей |
| [input.md](input.md) | единая экранная клавиатура и ввод для своих и чужих приложений |
| [shell-chrome.md](shell-chrome.md) | шторка уведомлений, long Home, менеджер задач |
| [player.md](player.md) | функции плеера, MPD 0.24, `idle`, `readpicture` |
| [reader.md](reader.md) | читалка как экран шелла: TXT, FB2, EPUB, пагинация |
| [apps.md](apps.md) | аудит приложений и библиотек, приоритеты P0/P1/P2 |
| [usb-modes.md](usb-modes.md) | USB: RNDIS, mass storage, charge-only, serial |
| [power.md](power.md) | питание, нагрев, suspend, поведение без АКБ |
| [hardware.md](hardware.md) | железо, ядро, сервисы, известные грабли |
| [qa.md](qa.md) | план проверок, burn-in, скрипты `qa/` |
| [font-medeo.md](font-medeo.md) | Medeo: tabular figures, baseline, внедрение |
| [icons.md](icons.md) | Phosphor/Lucide/Iconoir, выбор и интеграция |
| [master-plan.md](master-plan.md) | сводка: приоритеты, противоречия, критерии приёмки |

## Как читать

Начинать с `master-plan.md` — там сводка и порядок работ. Дальше по интересу:
`architecture.md` и `design-system.md` для устройства шелла, `settings-spec.md`
для самого большого куска UI, `hardware.md` для ограничений платформы.

Ссылки внутри текстов могут указывать на файлы исходного рабочего дерева
(`notes/`, `artifacts/`), которых в публичном репозитории нет.
