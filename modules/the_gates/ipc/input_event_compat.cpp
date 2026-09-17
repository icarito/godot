#include "input_event_compat.h"

#include "core/class_db.h"
#include "core/os/keyboard.h"
#include "core/print_string.h"
#include "variant_tools.h"

namespace {

struct PropertyRename {
	const char *godot4;
	const char *godot3;
};

// Only renames are listed. Names that match in both engines (pressed, echo,
// device, position, global_position, button_index, button_mask, index,
// pressure, tilt, factor, unicode, relative) pass through, and Godot 4 names
// with no Godot 3 counterpart (key_label, location, canceled, screen_relative,
// screen_velocity, pen_inverted) fall through to a no-op Object::set.
const PropertyRename PROPERTY_RENAMES[] = {
	{ "alt_pressed", "alt" },
	{ "shift_pressed", "shift" },
	{ "ctrl_pressed", "control" },
	{ "meta_pressed", "meta" },
	{ "keycode", "scancode" },
	{ "physical_keycode", "physical_scancode" },
	{ "double_click", "doubleclick" },
	{ "velocity", "speed" },
};

struct KeycodeRemap {
	int godot4_low;
	int godot3_low;
};

// Godot 4 inserted F17..F35 at 0x2C and renumbered everything above it.
// Codes at or below F16 (0x2B) and the keypad block (0x81..0x8F) are shared.
const KeycodeRemap KEYCODE_REMAPS[] = {
	{ 0x42, 0x2E }, // MENU
	{ 0x43, 0x2F }, // HYPER -> HYPER_L
	{ 0x45, 0x31 }, // HELP
	{ 0x48, 0x40 }, // BACK
	{ 0x49, 0x41 }, // FORWARD
	{ 0x4A, 0x42 }, // STOP
	{ 0x4B, 0x43 }, // REFRESH
	{ 0x4C, 0x44 }, // VOLUMEDOWN
	{ 0x4D, 0x45 }, // VOLUMEMUTE
	{ 0x4E, 0x46 }, // VOLUMEUP
	{ 0x54, 0x4C }, // MEDIAPLAY
	{ 0x55, 0x4D }, // MEDIASTOP
	{ 0x56, 0x4E }, // MEDIAPREVIOUS
	{ 0x57, 0x4F }, // MEDIANEXT
	{ 0x58, 0x50 }, // MEDIARECORD
	{ 0x59, 0x51 }, // HOMEPAGE
	{ 0x5A, 0x52 }, // FAVORITES
	{ 0x5B, 0x53 }, // SEARCH
	{ 0x5C, 0x54 }, // STANDBY
	{ 0x5D, 0x55 }, // OPENURL
	{ 0x5E, 0x56 }, // LAUNCHMAIL
	{ 0x5F, 0x57 }, // LAUNCHMEDIA
};

const int GODOT4_SPECIAL = 1 << 22;
const int GODOT4_CODE_MASK = GODOT4_SPECIAL - 1;
const int GODOT4_LAUNCH0 = 0x60;
const int GODOT4_LAUNCHF = 0x6F;
const int GODOT3_LAUNCH0 = 0x58;
const int GODOT4_F16 = 0x2B;
const int GODOT4_KP_FIRST = 0x81;
// Godot 4's UNKNOWN is CODE_MASK, whose low bits alias past the keypad block.
const int GODOT4_UNKNOWN_LOW = 0x3FFFFF;

String rename_property(const String &p_name) {
	for (size_t i = 0; i < sizeof(PROPERTY_RENAMES) / sizeof(PropertyRename); i++) {
		if (p_name == PROPERTY_RENAMES[i].godot4) {
			return PROPERTY_RENAMES[i].godot3;
		}
	}
	return p_name;
}

} // namespace

int tg_keycode_godot4_to_godot3(int p_keycode) {
	if (!(p_keycode & GODOT4_SPECIAL)) {
		return p_keycode;
	}

	const int low = p_keycode & GODOT4_CODE_MASK;
	if (low == GODOT4_UNKNOWN_LOW) {
		return KEY_UNKNOWN;
	}
	if (low <= GODOT4_F16 || low >= GODOT4_KP_FIRST) {
		return SPKEY | low;
	}
	if (low >= GODOT4_LAUNCH0 && low <= GODOT4_LAUNCHF) {
		return SPKEY | (GODOT3_LAUNCH0 + (low - GODOT4_LAUNCH0));
	}
	for (size_t i = 0; i < sizeof(KEYCODE_REMAPS) / sizeof(KeycodeRemap); i++) {
		if (KEYCODE_REMAPS[i].godot4_low == low) {
			return SPKEY | KEYCODE_REMAPS[i].godot3_low;
		}
	}
	return KEY_UNKNOWN;
}

Ref<InputEvent> tg_input_event_from_godot4_text(const String &p_text) {
	const String text = p_text.strip_edges();
	if (!text.begins_with("Object(") || !text.ends_with(")")) {
		return Ref<InputEvent>();
	}

	const String body = text.substr(7, text.length() - 8);
	const int split = body.find(",");
	if (split < 0) {
		return Ref<InputEvent>();
	}

	const String class_name = body.substr(0, split).strip_edges();
	if (!ClassDB::can_instance(class_name)) {
		return Ref<InputEvent>();
	}

	// Godot writes object properties as `"name":value` pairs, which is already
	// dictionary syntax, so the payload only needs rebracketing to be parsed
	// without ClassDB instantiating a Godot 4 class layout.
	const Variant parsed = str_to_var("{" + body.substr(split + 1) + "}");
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Ref<InputEvent>();
	}

	Object *obj = ClassDB::instance(class_name);
	InputEvent *event = Object::cast_to<InputEvent>(obj);
	if (event == nullptr) {
		if (obj != nullptr) {
			memdelete(obj);
		}
		return Ref<InputEvent>();
	}

	Ref<InputEvent> ref = Ref<InputEvent>(event);
	const Dictionary props = parsed;
	const Array keys = props.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String name = keys[i];
		Variant value = props[keys[i]];
		if (name == "keycode" || name == "physical_keycode") {
			value = tg_keycode_godot4_to_godot3((int)value);
		}
		bool valid = false;
		ref->set(rename_property(name), value, &valid);
	}

	return ref;
}
