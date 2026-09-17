#include "lockdown_engage.h"

#include "lockdown.h"

#include "core/print_string.h"

#include <stdlib.h>

namespace {

Vector<String> env_pipe_list(const char *p_name) {
	Vector<String> out;
	const char *value = ::getenv(p_name);
	if (value == nullptr || value[0] == '\0') {
		return out;
	}
	const Vector<String> split = String::utf8(value).split("|", false);
	for (int i = 0; i < split.size(); i++) {
		out.push_back(split[i]);
	}
	return out;
}

bool env_flag(const char *p_name) {
	const char *value = ::getenv(p_name);
	return value != nullptr && value[0] == '1';
}

} // namespace

void tg_lock_down_renderer() {
	const char *rw_dir = ::getenv("TG_SANDBOX_RW_DIR");
	const Error err = tg_apply_lockdown(rw_dir != nullptr ? String::utf8(rw_dir) : String(),
			env_pipe_list("TG_SANDBOX_RW_FILES"), env_pipe_list("TG_SANDBOX_RO_FILES"),
			env_flag("TG_SANDBOX_ALLOW_AUDIO"), env_flag("TG_SANDBOX_ALLOW_MICROPHONE"));
	if (err != OK) {
		CRASH_NOW_MSG("Sandbox lockdown failed; refusing to run gate code unconfined.");
	}
	print_line("[RENDERER-LOCKED] landlock + caps + seccomp");
}
