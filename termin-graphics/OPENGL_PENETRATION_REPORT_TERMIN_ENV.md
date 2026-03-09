# OpenGL Abstraction Leakage Report (`termin-env`)

Дата: 2026-03-04

Этот отчёт считает не «кто импортирует `tgfx` вообще», а именно где в `termin-env` протекают **OpenGL-специфичные части абстракции** из `termin-graphics/tgfx`.

## 1) Что считалось утечкой

Паттерны OpenGL-утечки из backend API:

- тип/инициализация: `OpenGLGraphicsBackend`, `init_opengl`
- GL-debug/state API: `check_gl_error`, `clear_gl_errors`, `reset_gl_state`
- GL/FBO-специфика хэндлов: `get_fbo_id`, `set_external_target`, `get_actual_gl_*`, `bind_framebuffer_id`, `fbo_id`

Отдельно считались прямые GL вызовы (обход абстракции): `gl*(` и `GL_*`.

## 2) Сводные цифры

Вне `termin-graphics`:

- GL-утечки через API/контракты: **56 файлов / 198 строк**
- Прямые GL вызовы (`gl*`, `GL_*`): **18 файлов / 237 строк**

В production-срезе (без docs/examples/tests/csharp wrappers):

- области: `termin/termin`, `termin/cpp`, `termin/core_c`, `termin-gui/python`, `termin-scene/src`, `diffusion-editor/diffusion_editor`, `termin-nodegraph/examples`
- всего файлов в срезе: **1076**
- GL-утечки через API/контракты: **37 файлов / 157 строк**
- прямые GL вызовы: **14 файлов / 171 строк**

## 3) Где течёт сильнее (production)

| Область | Файлов всего | GL-утечки API (файлы/строки) | Прямые GL вызовы (файлы/строки) |
|---|---:|---:|---:|
| `termin/termin` | 531 | 21 / 81 | 10 / 75 |
| `termin/cpp` | 360 | 14 / 70 | 2 / 77 |
| `termin-gui/python` | 66 | 1 / 4 | 1 / 17 |
| `diffusion-editor/diffusion_editor` | 26 | 1 / 2 | 0 / 0 |
| `termin-nodegraph/examples` | 1 | 1 / 2 | 1 / 2 |
| `termin/core_c` | 84 | 0 / 0 | 0 / 0 |
| `termin-scene/src` | 9 | 0 / 0 | 0 / 0 |

## 4) Симптомы утечки по типам API (production)

- `OpenGLGraphicsBackend`: **21 файлов / 73 строк**
- `init_opengl`: **5 / 8**
- `check_gl_error`: **6 / 25**
- `clear_gl_errors`: **1 / 2**
- `reset_gl_state`: **1 / 1**
- `get_fbo_id(...)`: **6 / 9**
- `set_external_target(...)`: **4 / 4**
- `get_actual_gl_*`: **3 / 20**
- `bind_framebuffer_id(...)`: **3 / 3**
- `fbo_id`: **6 / 13**

## 5) Главные точки фиксации

1. Runtime-дефолт OpenGL backend:
- `termin/termin/visualization/platform/backends/__init__.py` (`OpenGLGraphicsBackend.get_instance()`)

2. Протекание FBO/GL-полей в пользовательские и debug-контуры:
- `termin/cpp/termin/bindings/render/graphics_backend.cpp`
- `termin/cpp/termin/editor/frame_graph_debugger_core.cpp`
- `termin/termin/editor/framegraph_debugger.py`

3. GL-debug API в рабочих пассах:
- `termin/cpp/termin/render/color_pass.cpp`
- `termin/termin/visualization/render/posteffects/material_effect.py`

4. Прямой OpenGL (в обход нейтрального backend API):
- `termin/cpp/termin/render/wireframe_renderer.cpp`
- `termin/termin/visualization/platform/backends/fbo_backend.py`
- `termin/termin/visualization/render/texture.py`

## 6) Вывод

Проблема действительно не в «самом факте использования `tgfx`», а в том, что OpenGL-семантика протекла в контракты и runtime:

- фиксированный тип backend (`OpenGLGraphicsBackend`)
- FBO id и `get_actual_gl_*` в рабочем/debug API
- GL-specific debug/state методы в рендер-пайплайне
- местами прямые `gl*` вызовы

То есть абстракция backend сейчас частично номинальная: OpenGL-признаки видны за пределами `termin-graphics` как минимум в `termin/termin` и `termin/cpp`.
