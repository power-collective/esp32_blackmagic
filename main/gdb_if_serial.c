/*
 * This file is part of the Black Magic Debug project.
 *
 * Serial USB Serial/JTAG implementation of GDB interface for ESP32-C3
 * Uses USB Serial/JTAG Controller for GDB protocol communication
 */

#include <stdio.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "general.h"
#include "gdb_if.h"

extern target_s *cur_target;

static const char *TAG = "gdb_serial";

#define GDB_USB_BUF_SIZE 2048

static bool usb_serial_initialized = false;
/*
 * gdb_connected tracks whether the host COM port is currently open and
 * reading.  It starts false and is set true by gdb_if_serial_wait_connect().
 * It is set false by gdb_if_serial_flush() when a TX write times out,
 * which — per the CDC-ACM spec — is the only reliable signal that the
 * host side port is closed.
 */
static bool gdb_connected = false;

/* Initialize USB Serial/JTAG for GDB protocol */
bool gdb_if_init_serial(void)
{
    if (usb_serial_initialized) {
        return true;
    }

    usb_serial_jtag_driver_config_t usb_serial_config = {
        .rx_buffer_size = GDB_USB_BUF_SIZE,
        .tx_buffer_size = GDB_USB_BUF_SIZE,
    };

    esp_err_t ret = usb_serial_jtag_driver_install(&usb_serial_config);

    // ESP_ERR_INVALID_STATE means driver already installed (by console system) - that's OK!
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install USB Serial/JTAG driver: %d", ret);
        return false;
    }

    usb_serial_initialized = true;
    /* gdb_connected intentionally left false — the host has not opened the
     * COM port yet.  gdb_if_serial_wait_connect() will set it true once the
     * first RX data arrives, proving the host is present and listening. */

    ESP_LOGI(TAG, "GDB serial interface initialized on USB Serial/JTAG (driver state: %d)", ret);

    return true;
}

bool gdb_if_serial_is_connected(void)
{
    return gdb_connected && usb_serial_initialized;
}

/*
 * Drain all bytes currently sitting in the USB Serial/JTAG RX ring buffer.
 * Call this before starting a new GDB session to discard any stale bytes
 * that were buffered during a previous (possibly incomplete) session.
 */
static void serial_drain_rx(void)
{
    uint8_t discard[64];
    while (usb_serial_jtag_read_bytes(discard, sizeof(discard), 0) > 0)
        ;
}

/*
 * Block until the host opens the CDC-ACM port and sends the first byte,
 * then drain any additional stale bytes that were buffered from a previous
 * session.  Sets gdb_connected = true before returning.
 *
 * This is the serial-mode equivalent of accept() in TCP mode.  Call it
 * once per session before entering main_loop().
 *
 * Note: the triggering byte and any bytes drained after it are discarded.
 * GDB operates in ACK mode at session start (noackmode is reset by
 * gdb_bmp_state_reset() before this is called), so it will retransmit
 * qSupported automatically if it receives no ACK.
 */
void gdb_if_serial_wait_connect(void)
{
    uint8_t trigger;
    ESP_LOGI(TAG, "Waiting for host to open CDC-ACM port...");

    /* Block until at least one byte arrives — proof the host port is open. */
    while (usb_serial_jtag_read_bytes(&trigger, 1, portMAX_DELAY) != 1)
        ;

    /* Drain anything else already buffered (stale data from previous session). */
    serial_drain_rx();

    gdb_connected = true;
    ESP_LOGI(TAG, "Host connected, starting GDB session");
}

char gdb_if_serial_getchar(void)
{
    uint8_t c;
    int len;

    if (!usb_serial_initialized) {
        return '\x04'; // Return EOT if not initialized
    }

    // Block until we get a character
    while (1) {
        len = usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY);
        if (len == 1) {
            return (char)c;
        }

        /*
         * len < 0 means a driver error (e.g. during a USB disruption).
         * Return EOT so gdb_getpacket() exits cleanly rather than
         * injecting a spurious '+' ACK that would confuse the protocol.
         */
        if (len < 0) {
            ESP_LOGW(TAG, "USB Serial/JTAG read error: %d", len);
            return '\x04';
        }
        /* len == 0: spurious wakeup, loop and try again */
    }
}

char gdb_if_serial_getchar_to(uint32_t timeout)
{
    uint8_t c;
    int len;

    if (!usb_serial_initialized) {
        return (char)-1;
    }

    len = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(timeout));
    if (len == 1) {
        return (char)c;
    }

    return 0; // Timeout
}

static uint8_t buf[2048];
static size_t bufsize = 0;

void gdb_if_serial_flush(const bool force)
{
    if (!usb_serial_initialized || bufsize == 0) {
        return;
    }

    if (!force && bufsize < sizeof(buf)) {
        return;
    }

    int written = usb_serial_jtag_write_bytes((const char *)buf, bufsize, pdMS_TO_TICKS(100));
    bufsize = 0;

    /*
     * Per the CDC-ACM spec: when the host port is closed, the TX buffer
     * never drains and writes time out.  A zero-byte write (nothing sent
     * in 100 ms) is the only reliable signal that the port is closed.
     * Signal disconnection so main_loop() exits and the session can be
     * torn down cleanly.
     */
    if (written == 0) {
        ESP_LOGW(TAG, "TX timeout: host port closed, signalling disconnect");
        gdb_connected = false;
    }
}

void gdb_if_serial_putchar(char c, bool flush)
{
    if (!usb_serial_initialized) {
        return;
    }

    buf[bufsize++] = (uint8_t)c;
    if (flush || bufsize == sizeof(buf)) {
        gdb_if_serial_flush(true);
    }
}
