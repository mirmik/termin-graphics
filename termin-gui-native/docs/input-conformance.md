# Standard control input conformance

Native UI uses one desktop-style interaction contract. Hidden or effectively
disabled widgets are excluded by document routing before their control handler
is called. Primary pointer actions use the left button; middle and secondary
buttons are ignored unless a control explicitly documents a different gesture.

| Control | Focus | Pointer | Keyboard |
| --- | --- | --- | --- |
| `Button`, `IconButton` | Tab | left press/release with capture | Enter activates once on non-repeat key-down; Space arms on key-down and activates on key-up |
| `Checkbox` | Tab | left press/release with capture | Enter toggles once; Space arms and toggles on key-up |
| `Slider` | Tab | left drag with capture | arrows step; Page Up/Down use a large step; Home/End select bounds |
| `ComboBox` | Tab | left opens/selects | Enter opens/closes; arrows select; Escape closes |
| `TextInput` | Tab | left caret/selection drag | text editing and selection keys; Enter submits; CR/LF input and paste are normalized to spaces |
| `TextArea` | Tab | left caret/selection drag | multiline editing and navigation |
| `SpinBox` | Tab | left buttons or selection drag | arrows step; text editing keys while editing |
| `TabView` | Tab | left header selection | Left/Right and Page Up/Down wrap; Home/End select first/last |
| `ColorPicker` | Tab | left surface drag | Left/Right adjust hue, Up/Down value, Page Up/Down saturation, Home/End value bounds |
| `ScrollArea` | Tab or focused descendant | wheel chains at clamped boundaries; left scrollbar thumb drag | arrows scroll by line; Page Up/Down by viewport; Home/End select the primary-axis bounds |

Collection and editor views define their own documented navigation model, but
must still use left-button filtering for primary selection/activation and the
common pointer-cancel lifecycle for captured gestures.

Focus is visible through the default theme border overrides. Disabled controls
use distinct background, foreground and border colors. Key repeat is accepted
for navigation/value changes and suppressed for discrete activation.
