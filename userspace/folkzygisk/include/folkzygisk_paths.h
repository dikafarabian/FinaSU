/* FinaSU adaptation of ReZygisk: shared built-in engine/runtime paths.
 * Upstream copyright and license notices remain in LICENSE/NOTICE.
 */
#ifndef FOLKZYGISK_PATHS_H
#define FOLKZYGISK_PATHS_H

#define FOLKZYGISK_ENGINE_DIR "/data/adb/ksu/bin/folkzygisk"
#define HYOS_SPAWNER_PATH "/system_ext/bin/hyos_spawner"
#ifndef FOLKZYGISK_RUNTIME_DIR
#define FOLKZYGISK_RUNTIME_DIR "/data/adb/ksu/.zygisk"
#endif
#define FOLKZYGISK_STATE_FILE FOLKZYGISK_RUNTIME_DIR "/state.json"
#define FOLKZYGISK_MONITOR_LOCK FOLKZYGISK_RUNTIME_DIR "/monitor.lock"
/* Outside the runtime directory: ksud clears that on every boot. */
#define FOLKZYGISK_SPAWNER_MARKER "/data/adb/ksu/spawner_runtime"
#define FOLKZYGISK_DAEMON_BASE FOLKZYGISK_ENGINE_DIR "/bin/zygiskd"
#define FOLKZYGISK_TRACER_BASE FOLKZYGISK_ENGINE_DIR "/bin/zygisk-ptrace"

#endif
