#include "zmq_runtime.h"

#include "core/os/os.h"

#include "zmq.hpp"

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>
#elif OSX_ENABLED
#include <crt_externs.h>
#else
#include <stdio.h>
#endif

zmq::context_t &tg_zmq_context() {
	static zmq::context_t s_ctx;
	return s_ctx;
}

void tg_zmq_shutdown() {
	tg_zmq_context().close();
}

namespace {

#ifdef WINDOWS_ENABLED
Vector<String> read_process_argv() {
	Vector<String> argv;
	int argc = 0;
	LPWSTR *raw_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (raw_argv == nullptr) {
		return argv;
	}
	for (int i = 0; i < argc; i++) {
		argv.push_back(String(raw_argv[i]));
	}
	LocalFree(raw_argv);
	return argv;
}
#elif OSX_ENABLED
Vector<String> read_process_argv() {
	Vector<String> argv;
	const int argc = *_NSGetArgc();
	char **raw_argv = *_NSGetArgv();
	for (int i = 0; i < argc; i++) {
		argv.push_back(String::utf8(raw_argv[i]));
	}
	return argv;
}
#else
// The kernel's copy of argv, NUL-separated.
Vector<String> read_process_argv() {
	Vector<String> argv;
	FILE *f = fopen("/proc/self/cmdline", "rb");
	if (f == nullptr) {
		return argv;
	}

	Vector<char> current;
	int c = 0;
	while ((c = fgetc(f)) != EOF) {
		current.push_back((char)c);
		if (c == '\0') {
			argv.push_back(String::utf8(current.ptr()));
			current.clear();
		}
	}
	fclose(f);
	return argv;
}
#endif

} // namespace

Vector<String> tg_process_argv() {
	return read_process_argv();
}

String tg_cmdline_value(const String &p_flag) {
	if (p_flag == "--tg-ipc-dir" && !tg_ipc_dir_override.empty()) {
		return tg_ipc_dir_override;
	}
	if (p_flag == "--tg-user-data-dir" && !tg_user_data_dir_override.empty()) {
		return tg_user_data_dir_override;
	}

	// Godot strips the arguments it recognizes -- --resolution among them --
	// before OS::get_cmdline_args() is populated, so the launcher's values are
	// read from the process's own argv.
	const Vector<String> argv = read_process_argv();
	for (int i = 0; i + 1 < argv.size(); i++) {
		if (argv[i] == p_flag) {
			return argv[i + 1];
		}
	}

	const List<String> args = OS::get_singleton()->get_cmdline_args();
	for (const List<String>::Element *E = args.front(); E; E = E->next()) {
		if (E->get() == p_flag && E->next()) {
			return E->next()->get();
		}
	}
	return String();
}

String tg_resolve_ipc_address(const String &p_address) {
	const String marker = "user://";
	const int idx = p_address.find(marker);
	if (idx < 0) {
		return p_address;
	}
	const String prefix = p_address.substr(0, idx);
	const String suffix = p_address.substr(idx + marker.length());
	const String override = tg_cmdline_value("--tg-ipc-dir");
	const String dir = override.empty() ? OS::get_singleton()->get_user_data_dir() : override;
	return prefix + dir + "/" + suffix;
}
