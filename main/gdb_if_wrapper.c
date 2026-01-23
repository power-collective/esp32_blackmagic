/*
 * GDB Interface Runtime Mode Wrapper
 * Dispatches to either TCP or Serial interface based on runtime mode
 */

#include <stdbool.h>
#include "general.h"
#include "gdb_if.h"

// External declarations for TCP mode (gdb_if.c)
extern bool gdb_if_tcp_is_connected(void);
extern int gdb_if_tcp_init(void);
extern char gdb_if_tcp_getchar(void);
extern char gdb_if_tcp_getchar_to(uint32_t timeout);
extern void gdb_if_tcp_flush(const bool force);
extern void gdb_if_tcp_putchar(char c, bool flush);

// External declarations for Serial mode (gdb_if_serial.c)
extern bool gdb_if_serial_is_connected(void);
extern bool gdb_if_init_serial(void);
extern char gdb_if_serial_getchar(void);
extern char gdb_if_serial_getchar_to(uint32_t timeout);
extern void gdb_if_serial_flush(const bool force);
extern void gdb_if_serial_putchar(char c, bool flush);

// Runtime mode selection
static bool use_serial_mode = true; // Default to serial

void gdb_if_set_mode(bool serial)
{
    use_serial_mode = serial;
}

bool gdb_if_get_mode(void)
{
    return use_serial_mode;
}

// Wrapper functions that dispatch based on mode
bool gdb_if_is_connected(void)
{
    if (use_serial_mode) {
        return gdb_if_serial_is_connected();
    } else {
        return gdb_if_tcp_is_connected();
    }
}

int gdb_if_init(void)
{
    if (use_serial_mode) {
        return gdb_if_init_serial() ? 0 : -1;
    } else {
        return gdb_if_tcp_init();
    }
}

char gdb_if_getchar(void)
{
    if (use_serial_mode) {
        return gdb_if_serial_getchar();
    } else {
        return gdb_if_tcp_getchar();
    }
}

char gdb_if_getchar_to(uint32_t timeout)
{
    if (use_serial_mode) {
        return gdb_if_serial_getchar_to(timeout);
    } else {
        return gdb_if_tcp_getchar_to(timeout);
    }
}

void gdb_if_flush(const bool force)
{
    if (use_serial_mode) {
        gdb_if_serial_flush(force);
    } else {
        gdb_if_tcp_flush(force);
    }
}

void gdb_if_putchar(char c, bool flush)
{
    if (use_serial_mode) {
        gdb_if_serial_putchar(c, flush);
    } else {
        gdb_if_tcp_putchar(c, flush);
    }
}
