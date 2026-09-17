#ifndef TG_ZMQ_RUNTIME_H
#define TG_ZMQ_RUNTIME_H

#include "core/ustring.h"
#include "core/vector.h"

namespace zmq {
class context_t;
}

zmq::context_t &tg_zmq_context();
void tg_zmq_shutdown();

// The launcher's `--tg-ipc-dir` and `--tg-user-data-dir`, parsed in main.cpp.
// Both are empty when the binary runs outside TheGates.
extern String tg_ipc_dir_override;
extern String tg_user_data_dir_override;

// Value of a `--flag <value>` launcher argument. Arguments Godot itself
// consumes (`--resolution`) are not in OS::get_cmdline_args(), so the process's
// own argv is preferred and the cmdline list is the fallback.
String tg_cmdline_value(const String &p_flag);

// The process's own argv, including the arguments Godot consumed.
Vector<String> tg_process_argv();

String tg_resolve_ipc_address(const String &p_address);

#endif // TG_ZMQ_RUNTIME_H
