/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "general.h"
#include "gdb_if.h"
#include "version.h"

#include "gdb_packet.h"
#include "gdb_main.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "morse.h"
#include "driver/gpio.h"
#include "swd.h"
#include "jtagtap.h"

#include <assert.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform.h"

//#include <dhcpserver.h>

//#define ACCESS_POINT_MODE
#define AP_SSID	 "blackmagic"
#define AP_PSK	 "blackmagic"

//static char pbuf[GDB_PACKET_BUFFER_SIZE + 1U] __attribute__((aligned(8)));


//  | (1<<TMS_PIN) | (1<<TDI_PIN) | (1<<TDO_PIN) | (1<<TCK_PIN)
#define GPIO_OUTPUT_PIN_SEL  ((1<<SWCLK_PIN) | (1<<SWDIO_PIN))

// SWD clock divider: 0=fastest, higher=slower
// Empirically measured: divider=10 gives ~258kHz SWD clock
// Formula: actual_freq ≈ 2,580,000 / divider
// Start conservatively - many targets need slower clocks for reliable detection
// Can be changed via "monitor frequency" command (e.g., "monitor frequency 1M")
uint32_t target_clk_divider = 10;

void pins_init() {
    printf("pins_init: Configuring GPIO pins...\n");
    printf("  SWCLK_PIN = %d\n", SWCLK_PIN);
    printf("  SWDIO_PIN = %d\n", SWDIO_PIN);
    printf("  GPIO_OUTPUT_PIN_SEL = 0x%08lX\n", (unsigned long)GPIO_OUTPUT_PIN_SEL);

    gpio_config_t io_conf;

    // Configure SWCLK as output with pull-up
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << SWCLK_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  // Pull-up for stable idle state
    esp_err_t err = gpio_config(&io_conf);
    printf("pins_init: SWCLK gpio_config returned %d\n", err);
    // Set weak drive strength to reduce ringing (5mA)
    gpio_set_drive_capability(SWCLK_PIN, GPIO_DRIVE_CAP_0);
    printf("pins_init: SWCLK drive strength set to weak (5mA)\n");
    gpio_set_level(SWCLK_PIN, 0);  // Start with clock low

    // Configure SWDIO as output initially (will be switched dynamically)
    // with pull-up for stable idle state
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << SWDIO_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  // Pull-up for SWD spec compliance
    err = gpio_config(&io_conf);
    printf("pins_init: SWDIO gpio_config returned %d\n", err);
    // Set weak drive strength to reduce ringing (5mA)
    gpio_set_drive_capability(SWDIO_PIN, GPIO_DRIVE_CAP_0);
    printf("pins_init: SWDIO drive strength set to weak (5mA)\n");
    gpio_set_level(SWDIO_PIN, 1);  // Start high (idle state)

    // Configure NRST as open-drain output with pull-up
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT_OD;  // Open-drain for reset
    io_conf.pin_bit_mask = (1ULL << NRST_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    err = gpio_config(&io_conf);
    printf("pins_init: NRST gpio_config returned %d\n", err);
    gpio_set_level(NRST_PIN, 1);  // Release reset (high-Z with pull-up)
}

void platform_init()
{
	printf("platform_init: Starting platform initialization\n");

	/* Initialize GDB interface (serial or TCP) */
	printf("platform_init: Initializing GDB interface\n");
	gdb_if_init();

	pins_init();

	/* Initialize SWD and JTAG tap interfaces */
	printf("platform_init: Initializing SWD interface\n");
	swdptap_init();
	printf("platform_init: Initializing JTAG interface\n");
	jtagtap_init();

	printf("platform_init: Platform initialization complete\n");
}


void platform_nrst_set_val(bool assert)
{
	//gpio_set_val(NRST_PORT, NRST_PIN, !assert);
}

bool platform_nrst_get_val(void)
{
	return (gpio_get(NRST_PORT, NRST_PIN)) ? false : true;
}

void platform_srst_set_val(bool assert)
{
	(void)assert;
}

bool platform_srst_get_val(void) { return false; }

const char *platform_target_voltage(void)
{
	return "not supported on this platform.";
}

uint32_t platform_time_ms(void)
{
	//return xTaskGetTickCount() / portTICK_PERIOD_MS;
	int64_t time_milli=esp_timer_get_time()/1000;
	return((uint32_t)time_milli);
}

#define vTaskDelayMs(ms)	vTaskDelay((ms)/portTICK_PERIOD_MS)

void platform_delay(uint32_t ms)
{
	vTaskDelayMs(ms);
}

int platform_hwversion(void)
{
	return 0;
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}


void platform_max_frequency_set(const uint32_t frequency)
{
	if (frequency == 0) {
		// Invalid frequency, ignore
		return;
	}

	// Based on empirical measurement: divider=10 gives ~258kHz
	// This means: actual_freq ≈ 2,580,000 / divider
	// So: divider = 2,580,000 / desired_freq
	const uint32_t calibration_constant = 2580000U;

	uint32_t divider = calibration_constant / frequency;

	// Clamp divider to reasonable range
	// divider=0 is maximum speed (no delay loops)
	// divider=1000 would be ~2.58kHz, which is probably too slow but allowed
	if (divider > 1000U)
		divider = 1000U;

	target_clk_divider = divider;
}

uint32_t platform_max_frequency_get(void)
{
	// Based on empirical measurement: divider=10 gives ~258kHz
	// actual_freq ≈ 2,580,000 / divider
	const uint32_t calibration_constant = 2580000U;

	if (target_clk_divider == 0)
		// Maximum speed - return a reasonable estimate
		// With divider=0, no delay loops, limited only by GPIO speed
		// Estimate ~500kHz-1MHz based on typical ESP32 GPIO toggling speed
		return 500000U;

	return calibration_constant / target_clk_divider;
}


/* This is a transplanted main() from main.c */
void main_task(void *parameters)
{
	(void) parameters;

	platform_init();

	while (true) {

		TRY (EXCEPTION_ALL) {
			char* pbuf=gdb_packet_buffer();
			SET_IDLE_STATE(true);
			size_t size = gdb_getpacket(pbuf, GDB_PACKET_BUFFER_SIZE);
			// If port closed and target detached, stay idle
			if (pbuf[0] != '\x04' || cur_target)
				SET_IDLE_STATE(false);
			gdb_main(pbuf, GDB_PACKET_BUFFER_SIZE, size);
		}
		CATCH () {
		default:
			gdb_putpacketz("EFF");
			target_list_free();
			morse("TARGET LOST.", 1);
			break;
		}
	}

	/* Should never get here */
}

void user_init(void)
{

#if 0
	uart_set_baud(0, 460800);
	printf("SDK version:%s\n", sdk_system_get_sdk_version());

#ifndef ACCESS_POINT_MODE
	struct sdk_station_config config = {
		.ssid = WIFI_SSID,
		.password = WIFI_PASS,
	};

	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_set_config(&config);
#else

	/* required to call wifi_set_opmode before station_set_config */
	sdk_wifi_set_opmode(SOFTAP_MODE);

	struct ip_info ap_ip;
	IP4_ADDR(&ap_ip.ip, 172, 16, 0, 1);
	IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
	IP4_ADDR(&ap_ip.netmask, 255, 255, 0, 0);
	sdk_wifi_set_ip_info(1, &ap_ip);

	struct sdk_softap_config ap_config = {
		.ssid = AP_SSID,
		.ssid_hidden = 0,
		.channel = 3,
		.ssid_len = strlen(AP_SSID),
		.authmode = AUTH_OPEN, //AUTH_WPA_WPA2_PSK,
		.password = AP_PSK,
		.max_connection = 3,
		.beacon_interval = 100,
	};
	sdk_wifi_softap_set_config(&ap_config);

	ip_addr_t first_client_ip;
	IP4_ADDR(&first_client_ip, 172, 16, 0, 2);
	dhcpserver_start(&first_client_ip, 4);

#endif
#endif
	xTaskCreate(&main_task, "main", 4*256, NULL, 2, NULL);
}
