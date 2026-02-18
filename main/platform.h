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

#ifndef __PLATFORM_H
#define __PLATFORM_H

/* On ESP32-C6 (RISC-V), uint32_t is long unsigned int */
#undef PRIx32
#define PRIx32 "lx"

#undef PRIu32
#define PRIu32 "lu"

#undef SCNx32
#define SCNx32 "lx"

#define NO_USB_PLEASE

#define SET_RUN_STATE(state)
#define SET_IDLE_STATE(state)
#define SET_ERROR_STATE(state)
#define DEBUG(x, ...) do { ; } while (0)
//#define DEBUG printf

// Enable detailed SWD bit-level debugging
//#define DEBUG_SWD_BITS 1

#include "timing.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>

#define BOARD_IDENT "Black Magic Probe (esp32), (Firmware 0.2)"

#define TMS_SET_MODE() do { } while (0)

// No ports on the ESP32
#define TDI_PORT  0

// XIAO ESP32-C6 pinout:
// D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO21, D4=GPIO22, D5=GPIO23
// D6=GPIO16, D7=GPIO17, D8=GPIO19, D9=GPIO20, D10=GPIO18

// Pin definitions - can be overridden via build flags in platformio.ini
#ifndef TMS_PIN
#define TMS_PIN (21)  // Default: GPIO21 (M5Stack Atom Lite)
#endif

#ifndef TDI_PIN
#define TDI_PIN (22)  // Default: GPIO22 (M5Stack Atom Lite)
#endif

#ifndef TDO_PIN
#define TDO_PIN (19)  // Default: GPIO19 (M5Stack Atom Lite)
#endif

#ifndef TCK_PIN
#define TCK_PIN (25)  // Default: GPIO25 (M5Stack Atom Lite)
#endif

#ifndef SWDIO_PIN
#define SWDIO_PIN (21)  // Default: GPIO21 (M5Stack Atom Lite)
#endif

#ifndef SWCLK_PIN
#define SWCLK_PIN (25)  // Default: GPIO25 (M5Stack Atom Lite)
#endif

#define NRST_PORT 0
#ifndef NRST_PIN
#define NRST_PIN (23)  // Default: GPIO23 (M5Stack Atom Lite)
#endif


// On the ESP32 we dont have the PORTS (unlike stm32), this is dummy value to keep things similar as other platforms
#define SWCLK_PORT  0
#define SWDIO_PORT  0

/* M5Stack Atom Lite pin mapping for JTAG/SWD:
 * GPIO21 -> TMS/SWDIO
 * GPIO25 -> TCK/SWCLK
 * GPIO22 -> TDI
 * GPIO19 -> TDO
 * GPIO23 -> NRST
 * GPIO33 -> TRACESWO (optional)
 *
 * Available pins on Atom Lite headers:
 * Row 1: G21, G25
 * Row 2: G22, G19, G23, G33
 */
#define PLATFORM_IDENT "(ESP32C6)"
#define PLATFORM_HAS_TRACESWO 0
#define TRACESWO_PROTOCOL  2
#define SWO_ENCODING 2  /* UART mode for upstream command.c */

/* Enable platform-specific custom commands (uart_scan, uart_send) */
#define PLATFORM_HAS_CUSTOM_COMMANDS 1

#ifndef TRACESWO_PIN
#define TRACESWO_PIN 33  // Default: GPIO33 (M5Stack Atom Lite) - DISABLED
#endif

#ifndef TRACESWO_DUMMY_TX
#define TRACESWO_DUMMY_TX 18  // Default: D10 = GPIO18 - DISABLED
#endif

// UART configuration:
// UART0 = Console/GDB protocol (default USB serial)
// UART1 = Debug logs (GPIO26 TX, GPIO32 RX) - optional
#define PLATFORM_HAS_UART_PASSTHROUGH 0

// GDB Interface Mode Selection - RUNTIME SWITCHABLE
// Mode is selected at boot via button:
// - Button NOT pressed → Serial mode (default)
// - Button pressed (LOW) within 5s → WiFi mode
#ifndef MODE_SELECT_BUTTON_PIN
#define MODE_SELECT_BUTTON_PIN 39  // Default: GPIO39
#endif
#define MODE_SELECT_TIMEOUT_MS 5000

// Enable this to redirect debug logs to UART1 on GPIO26/32
// REQUIRED when GDB_SERIAL_MODE=1 to prevent log/GDB interference
#define USE_CUSTOM_DEBUG_UART 0

#if USE_CUSTOM_DEBUG_UART
#define DEBUG_UART_TX_PIN  26  // GPIO26 for debug log output
#define DEBUG_UART_RX_PIN  32  // GPIO32 for debug log input (optional)
#define DEBUG_UART_PORT    1   // Use UART1 for debug logs
#define DEBUG_UART_BAUD    115200
#endif


#define gpio_set_val(port, pin, value) do {	\
		if (pin>38) printf("__FUNCTION__%d",pin);  \
		gpio_set_level(pin, value);		\
		/* Small delay for GPIO settling - critical for reliable SWD */	\
		__asm__ __volatile__("nop; nop; nop; nop;");	\
	} while (0);

#define gpio_set(port, pin) gpio_set_val(port, pin, 1)
#define gpio_clear(port, pin) gpio_set_val(port, pin, 0)
#define gpio_get(port, pin) gpio_get_level(pin)

// TODO https://esp-idf.readthedocs.io/en/v2.0/api/peripherals/gpio.html#_CPPv216gpio_pull_mode_t
// GPIO_FLOATING
// 		gpio_enable(SWDIO_PIN, GPIO_INPUT);	
#define SWDIO_MODE_FLOAT() do {			\
		gpio_set_direction(SWDIO_PIN, GPIO_MODE_INPUT);		\
		gpio_set_pull_mode(SWDIO_PIN, GPIO_FLOATING);		\
	} while (0)

 //gpio_enable(SWDIO_PIN, GPIO_OUTPUT);		

#define SWDIO_MODE_DRIVE() do {				\
           gpio_set_direction(SWDIO_PIN, GPIO_MODE_OUTPUT);		\
	} while (0)

//#define PLATFORM_HAS_DEBUG 1
#define ENABLE_DEBUG 1
#endif
