/*
 * In-RAM ring-buffer capture of ESP-IDF log output.  See log_capture.h.
 *
 * The ESP-IDF logging layer funnels every ESP_LOGx call through a single
 * vprintf-like sink installed with esp_log_set_vprintf().  We install our own
 * sink that formats the line, appends it to a ring buffer, and (optionally)
 * chains to the sink that was previously installed so existing console
 * streaming is preserved.
 */
#include "log_capture.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

/* ~100 lines of history; device has 320 KB RAM, so this is cheap. */
#define LOG_CAPTURE_BUF_SIZE 8192

static char s_buf[LOG_CAPTURE_BUF_SIZE];
static size_t s_head;  /* next write position */
static size_t s_len;   /* bytes currently valid (<= BUF_SIZE) */

static vprintf_like_t s_prev_sink;
static bool s_also_stream;
static SemaphoreHandle_t s_lock;

static void buf_append(const char *data, size_t n)
{
    /* If the line is bigger than the whole buffer, keep only its tail. */
    if (n >= LOG_CAPTURE_BUF_SIZE) {
        data += (n - LOG_CAPTURE_BUF_SIZE);
        n = LOG_CAPTURE_BUF_SIZE;
    }
    for (size_t i = 0; i < n; ++i) {
        s_buf[s_head] = data[i];
        s_head = (s_head + 1) % LOG_CAPTURE_BUF_SIZE;
    }
    s_len += n;
    if (s_len > LOG_CAPTURE_BUF_SIZE)
        s_len = LOG_CAPTURE_BUF_SIZE;
}

static int log_capture_vprintf(const char *fmt, va_list args)
{
    char line[256];

    /*
     * Format once.  We need the args twice (capture + optional chain) but
     * va_list is single-use, so render into a local buffer and reuse that.
     */
    int len = vsnprintf(line, sizeof(line), fmt, args);
    if (len < 0)
        return len;

    size_t n = (size_t)len < sizeof(line) ? (size_t)len : sizeof(line) - 1;

    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        buf_append(line, n);
        xSemaphoreGive(s_lock);
    }

    /*
     * Preserve prior console streaming when asked (e.g. WiFi mode).  The
     * original va_list was consumed by vsnprintf above and can't be reused, so
     * forward the already-rendered line to the previous sink as a literal
     * "%s" argument.
     */
    if (s_also_stream && s_prev_sink)
        s_prev_sink("%s", line);

    return len;
}

void log_capture_init(bool also_stream)
{
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();

    s_also_stream = also_stream;
    /* Capture (and remember) the sink that was active before us. */
    s_prev_sink = esp_log_set_vprintf(log_capture_vprintf);
}

size_t log_capture_read(size_t offset, char *out, size_t max)
{
    if (!out || max == 0)
        return 0;

    size_t written = 0;
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (offset < s_len) {
            size_t avail = s_len - offset;
            size_t n = avail < max ? avail : max;
            /* Oldest byte is (head - len); step forward by offset from there. */
            size_t start = (s_head + LOG_CAPTURE_BUF_SIZE - s_len + offset) % LOG_CAPTURE_BUF_SIZE;
            for (size_t i = 0; i < n; ++i)
                out[i] = s_buf[(start + i) % LOG_CAPTURE_BUF_SIZE];
            written = n;
        }
        xSemaphoreGive(s_lock);
    }
    return written;
}

void log_capture_clear(void)
{
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_head = 0;
        s_len = 0;
        xSemaphoreGive(s_lock);
    }
}

size_t log_capture_size(void)
{
    return s_len;
}
