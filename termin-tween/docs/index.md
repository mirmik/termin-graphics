# termin-tween

`termin-tween` содержит независимое ядро твининга: easing-функции,
базовые tween-классы и `TweenManager`.

Пакет не зависит от editor/UI-слоя и не импортирует scene-компоненты при
`import termin.tween`.

Компонент `TweenManagerComponent` вынесен в `termin-components-tween`.
Канонический прямой импорт — `termin.tween_components`, а ленивый публичный
импорт `from termin.tween import TweenManagerComponent` сохраняется.
