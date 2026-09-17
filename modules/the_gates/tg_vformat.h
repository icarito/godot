#ifndef TG_VFORMAT_H
#define TG_VFORMAT_H

#include "core/array.h"
#include "core/ustring.h"
#include "core/variant.h"

// Godot 4 exposes printf-style vformat() over a Variant pack; Godot 3 only has
// String::sprintf(Array). Fold the arguments into an Array and forward so the
// ported call sites keep their format strings.
template <typename... Args>
String tg_vformat(const String &p_format, Args... p_args) {
	Array args;
	(args.append(p_args), ...);
	bool error = false;
	return p_format.sprintf(args, &error);
}

#endif // TG_VFORMAT_H
