/*
 * This file is part of the Black Magic Debug project.
 *
 * Serial UART implementation of GDB interface for ESP32
 * Uses UART0 for GDB protocol communication
 */

#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "general.h"
#include "gdb_if.h"

extern target_s *cur_target;

static const char *TAG = "gdb_serial";

#define GDB_UART_NUM UART_NUM_0
#define GDB_UART_BAUD 115200
#define GDB_UART_BUF_SIZE 2048

static bool uart_initialized = false;
static bool gdb_connected = false;

/* Initialize UART0 for GDB protocol */
bool gdb_if_init_serial(void)
{
    if (uart_initialized) {
        return true;
    }

    uart_config_t uart_config = {
        .baud_rate = GDB_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t ret = uart_param_config(GDB_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %d", ret);
        return false;
    }

    // Use default pins for UART0 (GPIO43/44 on ESP32-C6, varies by chip)
    ret = uart_set_pin(GDB_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return false;
    }

    ret = uart_driver_install(GDB_UART_NUM, GDB_UART_BUF_SIZE * 2, GDB_UART_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return false;
    }

    uart_initialized = true;
    gdb_connected = true; // Serial is always "connected"

    ESP_LOGI(TAG, "GDB serial interface initialized on UART%d @ %d baud", GDB_UART_NUM, GDB_UART_BAUD);
    return true;
}

bool gdb_if_serial_is_connected(void)
{
    return gdb_connected && uart_initialized;
}

char gdb_if_serial_getchar(void)
{
    uint8_t c;
    int len;

    if (!uart_initialized) {
        return '\x04'; // Return EOT if not initialized
    }

    // Block until we get a character
    while (1) {
        len = uart_read_bytes(GDB_UART_NUM, &c, 1, portMAX_DELAY);
        if (len == 1) {
            return (char)c;
        }

        // If we somehow get here with an error, return ACK to keep protocol moving
        if (len < 0) {
            ESP_LOGW(TAG, "UART read error: %d", len);
            return '+';
        }
    }
}

char gdb_if_serial_getchar_to(uint32_t timeout)
{
    uint8_t c;
    int len;

    if (!uart_initialized) {
        return (char)-1;
    }

    len = uart_read_bytes(GDB_UART_NUM, &c, 1, pdMS_TO_TICKS(timeout));
    if (len == 1) {
        return (char)c;
    }

    return 0; // Timeout
}

static uint8_t buf[2048];
static size_t bufsize = 0;

void gdb_if_serial_flush(const bool force)
{
    if (!uart_initialized || bufsize == 0) {
        return;
    }

    if (!force && bufsize < sizeof(buf)) {
        return;
    }

    uart_write_bytes(GDB_UART_NUM, (const char *)buf, bufsize);
    uart_wait_tx_done(GDB_UART_NUM, pdMS_TO_TICKS(100));
    bufsize = 0;
}

void gdb_if_serial_putchar(char c, bool flush)
{
    if (!uart_initialized) {
        return;
    }

    buf[bufsize++] = (uint8_t)c;
    if (flush || bufsize == sizeof(buf)) {
        gdb_if_serial_flush(true);
    }
}
