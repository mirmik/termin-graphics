# Line Rendering Plan

Цель: сделать в `termin-graphics` полноценный line rendering stack, где
компоненты верхнего уровня только передают данные, а вся математика и GPU
отрисовка линий живет в tgfx2.

## 1. Модель данных

Вынести общие типы линий в `termin-graphics`:

- `LineWidthMode::World` и `LineWidthMode::ScreenPixels`
- `LineCapStyle::Butt`, `Square`, `Round`
- `LineJoinStyle::Bevel`, `Miter`, `Round`
- `LinePoint`, `LineStyle`, `LineBatch`

Важно явно различать world-space ширину и screen-space ширину в пикселях.
Неявное переключение по renderer/backend сделает поведение трудно
проверяемым.

## 2. CPU reference path

Текущий `line_mesh_builder` остается CPU reference/fallback path для
world-space линий:

- полноценный bevel join
- miter join с `miter_limit`
- round join как дуга между offset-направлениями, а не disk-заглушка
- closed polylines
- degenerate cases: одинаковые точки, нулевая ширина, острые углы,
  почти 180 градусов
- unit-тесты на геометрию и количество треугольников

Этот путь нужен для CAD/world geometry и как тестируемый эталон.

## 3. Instanced draw в tgfx2

Добавить backend-neutral instanced draw API:

```cpp
ICommandList::draw_instanced(vertex_count, instance_count,
                             first_vertex, first_instance);
RenderContext2::draw_arrays_instanced(vbo, vertex_count, instance_count);
```

Backend implementation:

- OpenGL: `glDrawArraysInstanced`
- Vulkan: `vkCmdDraw(vertexCount, instanceCount, firstVertex, firstInstance)`

Параллельно проверить и довести путь `VertexBufferLayout::per_instance`, чтобы
instance buffers были частью публичного render path.

## 4. GPU segment renderer

Добавить screen-space renderer в `termin-graphics`, например
`ScreenSpaceLineRenderer`.

Первый вариант рисует только тела сегментов. CPU готовит компактный instance
buffer:

```cpp
struct LineSegmentInstance {
    Vec3 p0;
    float width_px;
    Vec3 p1;
    float flags;
    Vec4 color0;
    Vec4 color1;
};
```

Vertex shader на каждый instance генерирует 6 вершин quad:

- проектирует `p0/p1` через view-projection
- считает направление в screen/NDC
- строит perpendicular
- смещает вершины на `width_px / viewport`
- получает camera-facing quad со стабильной экранной толщиной

## 5. Caps и joins

Не делать один сверхсложный shader. Разделить renderer на несколько stream-ов:

- segment body instances
- cap instances
- join instances

Сначала:

- `Butt` без дополнительной геометрии
- `Square` через удлинение segment body
- `Bevel join`

Затем:

- `Round cap`
- `Miter join` с limit
- `Round join`

## 6. Near-plane и clipping

Зафиксировать политику clipping:

```cpp
enum class LineClipMode {
    DropBehindCamera,
    ClipToNearPlane,
};
```

Production path должен прийти к `ClipToNearPlane`, иначе orbit camera будет
ломать screen-space направление на сегментах, пересекающих near plane.

## 7. Example

Расширить line example:

- CPU world-space mesh path
- GPU screen-space instanced path
- raw backend line path для сравнения
- оси, helix, closed loop, острые углы, разные widths
- caps/joins modes

## 8. Visual tests

Добавить offscreen visual tests:

- stable pixel width at different camera distances
- camera-facing line during orbit
- round cap visible
- bevel join without gap
- miter limit behavior

## 9. Подключение LineRenderer

`termin::LineRenderer` должен стать тонким adapter-ом:

- хранит points/style/material-ish настройки
- выбирает `WorldMesh` или `ScreenSpace`
- делегирует математику и draw path в `termin-graphics`

Публичный API должен быть явным:

```cpp
renderer.width_mode = LineWidthMode::ScreenPixels;
renderer.width = 3.0f;
```

## 10. Cleanup

После появления GPU path:

- убрать временные заглушки в joins
- привести examples к новому API
- убрать дублирование attachment helpers
- обновить docs и migration notes
