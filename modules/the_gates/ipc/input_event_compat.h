#ifndef TG_INPUT_EVENT_COMPAT_H
#define TG_INPUT_EVENT_COMPAT_H

#include "core/os/input_event.h"
#include "core/reference.h"

// Rebuilds a Godot 3 InputEvent from the text Variant a Godot 4 launcher wrote.
// Returns null when the payload is not an `Object(Class,...)` form or names a
// class this engine does not have.
Ref<InputEvent> tg_input_event_from_godot4_text(const String &p_text);

// Godot 4 numbers special keys from `SPECIAL = 1 << 22`, Godot 3 from
// `SPKEY = 1 << 24`, and Godot 4 inserted F17..F35 at 0x2C, shifting every
// code above it. Plain Unicode keycodes are identical in both engines.
int tg_keycode_godot4_to_godot3(int p_keycode);

#endif // TG_INPUT_EVENT_COMPAT_H
