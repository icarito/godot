#ifndef TG_LOCKDOWN_ENGAGE_H
#define TG_LOCKDOWN_ENGAGE_H

// Applies the 4.x fork's Linux lockdown (no_new_privs, landlock, capability
// drop, seccomp) with the policy the launcher's SandboxLinux::spawn_target put
// in the TG_SANDBOX_* environment. Crashes rather than run gate code unconfined.
void tg_lock_down_renderer();

#endif // TG_LOCKDOWN_ENGAGE_H
