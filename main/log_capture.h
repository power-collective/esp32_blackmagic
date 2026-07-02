/*
 * In-RAM capture of ESP-IDF log output so it can be retrieved on demand via
 * the "monitor get_logs" GDB command instead of being streamed raw onto the
 * shared USB-Serial-JTAG port (where, in serial GDB mode, it would interleave
 * with and corrupt the GDB packet stream).
 */
#ifndef LOG_CAPTURE_H
#define LOG_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Install the capturing log hook.
 *
 * All ESP_LOGx / printf-to-log output is appended to an internal ring buffer.
 * If `also_stream` is true the output is ALSO forwarded to whatever log sink
 * was installed before this call (e.g. the UART/USB console) — use that in
 * WiFi mode where the console is not shared with GDB.  In serial GDB mode pass
 * false so nothing reaches the wire and the GDB stream stays clean; the logs
 * are still captured and retrievable with get_logs.
 *
 * Safe to call once during startup, before other logging matters.
 */
void log_capture_init(bool also_stream);

/*
 * Copy up to `max` bytes of captured log history, starting `offset` bytes from
 * the oldest captured byte, into `out`.  Returns the number of bytes written
 * (0 once `offset` reaches the end).  Does not NUL-terminate.  Reading in
 * advancing `offset` windows walks the whole history oldest-first.
 */
size_t log_capture_read(size_t offset, char *out, size_t max);

/* Discard all captured log history. */
void log_capture_clear(void);

/* Number of bytes currently held (capped at the buffer size). */
size_t log_capture_size(void);

#endif /* LOG_CAPTURE_H */
