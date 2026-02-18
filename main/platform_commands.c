/*
 * ESP32-specific platform commands for Black Magic Probe
 *
 * This file provides ESP32-specific GDB monitor commands
 * that extend the upstream command.c functionality.
 */

#include "general.h"
#include "target_internal.h"
#include "gdb_packet.h"
#include "platform.h"
#include "timing.h"
#include "esp_system.h"

/* External functions from other ESP32 modules */
extern void scan_uart_boot_mode(void);
extern void send_to_uart(int argc, const char **argv);
extern void gdb_bmp_state_reset(void);
extern void platform_request_bmp_reset(void);
extern void platform_mode_save(bool use_serial);
extern bool gdb_if_get_mode(void);

/*
 * uart_scan command - Scan for STM32 in UART boot mode
 * The target must be in boot mode for this to work.
 */
static bool cmd_uart_scan(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	scan_uart_boot_mode();
	return true;
}

/*
 * uart_send command - Send bytes over the TRACESWO_DUMMY_TX pin
 * Usage: mon uart_send <data>
 */
static bool cmd_uart_send(target_s *t, int argc, const char **argv)
{
	(void)t;
	if (argc < 2) {
		gdb_out("Usage: uart_send <data>\n");
		return false;
	}
	send_to_uart(argc, argv);
	gdb_outf("Sending: %s\n", argv[1]);
	platform_delay(500);
	return true;
}

/*
 * swd_test command - Test SWD pin connectivity
 * Toggles SWCLK and checks if SWDIO can be read back
 */
static bool cmd_swd_test(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;

	gdb_out("SWD Pin Test\n");
	gdb_out("============\n");
	gdb_outf("SWDIO Pin: GPIO%d\n", SWDIO_PIN);
	gdb_outf("SWCLK Pin: GPIO%d\n", SWCLK_PIN);
	gdb_outf("NRST Pin:  GPIO%d\n", NRST_PIN);
	gdb_out("\n");

	// Ensure pins are configured as outputs first
	gdb_out("Configuring pins...\n");

	esp_err_t err1 = gpio_set_direction(SWCLK_PIN, GPIO_MODE_OUTPUT);
	esp_err_t err2 = gpio_set_direction(SWDIO_PIN, GPIO_MODE_OUTPUT);

	if (err1 != ESP_OK) {
		gdb_outf("ERROR: Failed to set SWCLK direction: %d\n", err1);
	}
	if (err2 != ESP_OK) {
		gdb_outf("ERROR: Failed to set SWDIO direction: %d\n", err2);
	}

	platform_delay(10);
	gdb_out("Pins configured\n\n");

	// Test SWCLK output with a steady signal
	gdb_out("Testing SWCLK (clock) output:\n");
	gdb_out("Setting SWCLK HIGH for 3 seconds (measure with multimeter)...\n");

	gpio_set_level(SWCLK_PIN, 1);
	platform_delay(3000);

	gdb_out("Setting SWCLK LOW for 3 seconds (measure with multimeter)...\n");
	gpio_set_level(SWCLK_PIN, 0);
	platform_delay(3000);

	gdb_out("SWCLK test complete\n");
	gdb_out("Expected: ~3.3V when HIGH, ~0V when LOW\n");
	gdb_out("\n");

	// Test SWDIO bidirectional
	gdb_out("Testing SWDIO bidirectional:\n");

	// Test output
	gpio_set_direction(SWDIO_PIN, GPIO_MODE_OUTPUT);
	platform_delay(1);

	gpio_set_level(SWDIO_PIN, 1);
	platform_delay(1);
	int swdio_high = gpio_get_level(SWDIO_PIN);

	gpio_set_level(SWDIO_PIN, 0);
	platform_delay(1);
	int swdio_low = gpio_get_level(SWDIO_PIN);

	gdb_outf("  Output: HIGH=%d LOW=%d %s\n", swdio_high, swdio_low,
	         (swdio_high && !swdio_low) ? "OK" : "FAIL");

	// Test input (should float high with target's pull-up)
	gpio_set_direction(SWDIO_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(SWDIO_PIN, GPIO_FLOATING);
	platform_delay(5);
	int swdio_float = gpio_get_level(SWDIO_PIN);
	gdb_outf("  Input (floating): %d %s\n", swdio_float,
	         swdio_float ? "OK (pulled high)" : "LOW (check target power/connection)");

	gdb_out("\n");
	gdb_out("If all tests show OK, wiring is likely correct.\n");
	gdb_out("If input shows LOW, check:\n");
	gdb_out("  - Target is powered\n");
	gdb_out("  - SWDIO is connected\n");
	gdb_out("  - Target has pull-up on SWDIO\n");

	return true;
}

/*
 * bmp_reset command - reset the BMP probe state machine without rebooting.
 *
 * Useful when the probe gets stuck in a weird state (e.g. target was
 * disconnected mid-session, noackmode is stuck on, or cur_target is stale).
 * After this command, use 'monitor swdp_scan' to re-enumerate targets.
 *
 * If the probe is currently stuck inside the target-running poll loop
 * (i.e. this command can't be reached), send Ctrl+C from GDB.  The halt
 * timeout will automatically fire after 5 s and perform the same reset.
 */
static bool cmd_bmp_reset(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	gdb_out("Resetting BMP probe state...\n");
	/*
	 * Signal the poll loop in case it is running (races are harmless here
	 * because the flag is checked on every poll iteration).
	 */
	platform_request_bmp_reset();
	/*
	 * Also reset directly: if we are here then we are NOT inside the running
	 * poll loop, so it is safe to call immediately.
	 */
	gdb_bmp_state_reset();
	gdb_out("BMP reset complete. Use 'monitor swdp_scan' to re-scan targets.\n");
	return true;
}

/*
 * Platform-specific command list
 * This is referenced by upstream command.c when PLATFORM_HAS_CUSTOM_COMMANDS is defined
 */
const command_s platform_cmd_list[] = {
	{"uart_scan", cmd_uart_scan, "STM32 UART boot mode scan on TRACESWO pin"},
	{"uart_send", cmd_uart_send, "Send bytes on TRACESWO_DUMMY_TX pin"},
	{"swd_test", cmd_swd_test, "Test SWD pin connectivity and wiring"},
	{"bmp_reset", cmd_bmp_reset, "Reset BMP probe state machine (no reboot)"},
	{NULL, NULL, NULL},
};
