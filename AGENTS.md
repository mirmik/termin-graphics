# AGENTS.md

## Scope
These instructions apply to everything inside `/home/mirmik/project/termin-env`.

## Collaboration style
Не обязательно спрашивать подтверждение на каждый шаг.
Можно работать автономно на более длинных этапах, делая промежуточные отчеты по мере прогресса.

## Сборка и тестирование

Основной способ сборки всего SDK — `./build-sdk.sh`. Он вызывает сборку C++ (`build-sdk-cpp.sh`), биндингов (`build-sdk-bindings.sh`) и pip-пакетов. После однократной сборки для запуска тестов **не нужно ничего устанавливать в систему** — достаточно venv с editable-установками пакетов.

### Первичная настройка тестового окружения

```bash
./build-sdk.sh                           # полная сборка SDK (один раз)
./setup-test-venv.sh                     # создаёт .venv/ и ставит пакеты в editable-режиме
```

`setup-test-venv.sh` создаёт виртуальное окружение, устанавливает зависимости (nanobind, numpy, pytest) и запускает `install-pip-packages.sh --editable` для всех пакетов. Editable-режим означает, что Python-код пакетов импортируется прямо из исходников — изменения применяются мгновенно, без переустановки.

### Ежедневный цикл разработки

```bash
source .venv/bin/activate                # активировать venv
bash run-tests-python.sh                 # прогнать Python-тесты
# ... правки в Python-коде ...
bash run-tests-python.sh                 # сразу видит изменения (editable)
```

`run-tests-python.sh` автоматически находит `.venv/bin/python3`, если он существует, и выставляет `TERMIN_SDK` — поэтому при использовании venv ничего экспортировать вручную не нужно.

### После пересборки C++ биндингов

Если менялся C++ код и запускался `./build-sdk-bindings.sh`, нужно обновить `.so`-файлы в исходниках пакетов:

```bash
./setup-test-venv.sh --force             # перекопирует .so из SDK в исходники
```

### Как это работает под капотом

Два уровня нативных библиотек:

1. **Python-биндинги** (nanobind `.so`-расширения) — `TerminCMakeBuildExt` копирует их из `$TERMIN_SDK/lib/python/` в `<pkg>/python/<pkg>/` при `pip install`. В editable-режиме `.so` лежат рядом с `.py` файлами и подхватываются автоматически.

2. **C++ shared-библиотеки** (`libtermin_*.so`) — лежат в `$TERMIN_SDK/lib/`. Каждый `__init__.py` при импорте вызывает `preload_sdk_libs(...)` из `termin_nanobind.runtime`, которая загружает их через `ctypes.CDLL` с `RTLD_GLOBAL`. Это делает ненужным `LD_LIBRARY_PATH` (хотя `run-tests-python.sh` выставляет его как подстраховку).

`TERMIN_SDK` определяется автоматически: переменная окружения → `./sdk` → `/opt/termin`.

## Nested projects
If a task is inside a subproject that has its own `AGENTS.md`, read and follow it before doing work.
Nested `AGENTS.md` files override this file for their own subtree.
