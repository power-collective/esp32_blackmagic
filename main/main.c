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

/* Provides main entry point.  Initialise subsystems and enter GDB
 * protocol loop.
 */


#include "general.h"
#include "gdb_if.h"
#include "gdb_main.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "morse.h"
#include "libopencm3/cm3/common.h"


#include "command.h"
#ifdef ENABLE_RTT
#include "rtt.h"
#endif

#if PLATFORM_HAS_UART_PASSTHROUGH
#include "uart_passthrough.h"
#endif

#include "web_server.h"


#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#else
#include "esp_event_loop.h"
#endif

#include "esp_wifi.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"

//
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "driver/uart.h"
#include <stdarg.h>

unsigned short gdb_port = 2345;
#include "platform.h"

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define AP_MODE 1

//#ifndef AP_MODE
//#define USE_DHCP 1
//#ifndef USE_DHCP
//esp_netif_ip_info_t ip_info = {
//    .ip = {
//        .addr = ESP_IP4TOADDR(192, 168, 1, 22),
//    },
//    .gw = {
//        .addr = ESP_IP4TOADDR(192, 168, 1, 1),
//    },
//    .netmask = {
//        .addr = ESP_IP4TOADDR(255, 255, 255, 0),
//    },
//};
//#endif
//#endif

/* This has to be aligned so the remote protocol can re-use it without causing Problems */
static char pbuf[GDB_PACKET_BUFFER_SIZE + 1U] __attribute__((aligned(8)));

/*
 * How long (ms) to wait for a target to acknowledge a halt request before
 * giving up and force-resetting the BMP state.  Five seconds is generous;
 * a healthy Cortex-M responds within a handful of SWD clock cycles.
 */
#define TARGET_HALT_TIMEOUT_MS 5000U

/*
 * Set this from anywhere (ISR, timer, monitor command) to break out of
 * the target-running poll loop and perform a clean state reset on the
 * next poll iteration.
 */
volatile bool bmp_reset_requested = false;

void platform_request_bmp_reset(void)
{
	bmp_reset_requested = true;
}

char *gdb_packet_buffer()
{
	return pbuf;
}

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD  WIFI_AUTH_WEP 
#define ESP_WIFI_SAE_MODE                  ESP_WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER             ""
static const char *TAG = "blackmagic";




// extern
void set_gdb_socket(int socket);
void set_gdb_listen(int socket);
bool gdb_if_is_connected(void);
bool gdb_if_get_mode(void);
void gdb_if_tcp_clear_buffers(void);

static int s_retry_num = 0;

static int already_connected=0;

#define EXAMPLE_ESP_MAXIMUM_RETRY  10


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            if (already_connected==0) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            }
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        already_connected=1;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

#ifndef AP_MODE
static void initialise_wifi(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    ESP_LOGI(TAG, "Getting IP addresses from DHCP");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = true;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .channel = 0,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}
#else
void wifi_init_softap(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = true;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_WIFI_SSID,
            .ssid_len = strlen(CONFIG_WIFI_SSID),
            .channel = 1,
            .password = CONFIG_WIFI_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    if (strlen(CONFIG_WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD, 1);
}
#endif

void old_gdb_application_thread(void *pvParameters)
{
	int sock, new_sd;
	struct sockaddr_in address, remote;
	int size;

    ESP_LOGI(TAG, "create socket");
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return;

	address.sin_family = AF_INET;
	address.sin_port = htons(gdb_port);
	address.sin_addr.s_addr = INADDR_ANY;

    ESP_LOGI(TAG, "bind");
	if (bind(sock, (struct sockaddr *)&address, sizeof (address)) < 0)
		return;

	listen(sock, 0);
    set_gdb_listen(sock);

	size = sizeof(remote);

    ESP_LOGI(TAG, "accept");

	while (1) {
		if ((new_sd = accept(sock, (struct sockaddr *)&remote, (socklen_t *)&size)) > 0) {
			    printf("accepted new gdb connection 1\n");
                set_gdb_socket(new_sd);
                gdb_main(pbuf, GDB_PACKET_BUFFER_SIZE, size);
	        }
	}
}


static void bmp_poll_loop(void)
{
	if (!gdb_if_is_connected())
		return;

	SET_IDLE_STATE(false);

	bool halt_was_requested = false;
	uint32_t halt_deadline_ms = 0;

	while (gdb_target_running && cur_target) {
		if (!gdb_if_is_connected())
			return;

		/* Honor an async reset request (from monitor command or future button ISR) */
		if (bmp_reset_requested) {
			bmp_reset_requested = false;
			gdb_bmp_state_reset();
			gdb_putpacketz("X1D");
			return;
		}

		gdb_poll_target();

		// Check again, as `gdb_poll_target()` may
		// alter these variables.
		if (!gdb_target_running || !cur_target)
			break;
		char c = gdb_if_getchar_to(0);
		if (c == '\x03' || c == '\x04') {
			target_halt_request(cur_target);
			/* Start the halt timeout on the first Ctrl+C */
			if (!halt_was_requested) {
				halt_was_requested = true;
				halt_deadline_ms = platform_time_ms() + TARGET_HALT_TIMEOUT_MS;
			}
		}

		/*
		 * If the target was asked to halt but never responded within the
		 * timeout, the SWD connection is likely dead.  Force a clean state
		 * reset rather than spinning forever.
		 */
		if (halt_was_requested && platform_time_ms() > halt_deadline_ms) {
			gdb_bmp_state_reset();
			gdb_putpacketz("X1D");
			return;
		}

		platform_pace_poll();
#ifdef ENABLE_RTT
		if (rtt_enabled)
			poll_rtt(cur_target);
#endif
	}

	if (!gdb_if_is_connected())
		return;

	SET_IDLE_STATE(true);
	size_t size = gdb_getpacket(pbuf, GDB_PACKET_BUFFER_SIZE);
	if (!gdb_if_is_connected())
		return;
	// If port closed and target detached, stay idle
	if (pbuf[0] != '\x04' || cur_target)
		SET_IDLE_STATE(false);
	gdb_main(pbuf, GDB_PACKET_BUFFER_SIZE, size);
}

static void bad_bmp_poll_loop(void)
{
	SET_IDLE_STATE(false);
	while (gdb_target_running && cur_target) {
		gdb_poll_target();

		// Check again, as `gdb_poll_target()` may
		// alter these variables.
		if (!gdb_target_running || !cur_target)
			break;
		char c = gdb_if_getchar_to(0);
		if (c == '\x03' || c == '\x04')
			target_halt_request(cur_target);
		platform_pace_poll();
#ifdef ENABLE_RTT
		if (rtt_enabled)
			poll_rtt(cur_target);
#endif
	}

    while (1) {
        SET_IDLE_STATE(true);

        //char c = gdb_if_getchar_to(0);
		//if (c  == '\x03' || c  == '\x04') {
		//	target_halt_request(cur_target);
        //    printf("halt");
        //}
        //if (c!=0) {
        //    pbuf[0]=c;
        //}

        size_t size = gdb_getpacket(pbuf, GDB_PACKET_BUFFER_SIZE);
		if (pbuf[0]  == '\x03' || pbuf[0]  == '\x04') {
			target_halt_request(cur_target);
            if (cur_target) printf("halting\n");
            int retries=6;

            target_addr64_t watch;
	        target_halt_reason_e reason = target_halt_poll(cur_target, &watch);
	        while (!reason && retries-->0) {
                reason = target_halt_poll(cur_target, &watch);
            }
            gdb_poll_target();
        }
        // If port closed and target detached, stay idle
        if (pbuf[0] != '\x04' || cur_target)
            SET_IDLE_STATE(false);

        gdb_main(pbuf, GDB_PACKET_BUFFER_SIZE, size);

    }
}

static void main_loop(void)
{
     while (gdb_if_is_connected()) {
		TRY (EXCEPTION_ALL) {
			bmp_poll_loop();
		}
		CATCH () {
		default:
			gdb_putpacketz("EFF");
			target_list_free();
			gdb_outf("Uncaught exception: %s\n", exception_frame.msg);
			morse("TARGET LOST.", true);
			break;
		}
     }
     // Only log in TCP mode - serial mode shares UART0 with console
     if (!gdb_if_get_mode()) {
         ESP_LOGI(TAG, "GDB connection closed, waiting for new connection");
     }
}


static void gdb_application_thread(void *pvParameters)
{
    char addr_str[128];
    int addr_family = AF_INET; // (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = 5;
    int keepInterval = 10;
    int keepCount = 4;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(gdb_port);
        ip_protocol = IPPROTO_TCP; // IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    //int opt = 1;
    //setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    // setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", gdb_port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        set_gdb_listen(listen_sock);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);
        printf("accepted new gdb connection\n");
        set_gdb_socket(sock);
        gdb_if_tcp_clear_buffers();  // Clear any stale data from previous connection
        main_loop();

        // Clean up after connection closed
        close(sock);
        ESP_LOGI(TAG, "GDB socket closed, ready for new connection");
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void main_task(void *parameters);

// External function to set GDB interface mode
extern void gdb_if_set_mode(bool serial);

/* NVS namespace and key used to persist the GDB interface mode. */
#define NVS_MODE_NAMESPACE "blackmagic"
#define NVS_MODE_KEY       "gdb_mode"

/*
 * Load the previously saved mode from NVS.
 * Returns the saved value, or `default_mode` if no entry exists yet.
 * true = USB serial, false = WiFi.
 */
static bool mode_nvs_load(bool default_mode)
{
    nvs_handle_t h;
    if (nvs_open(NVS_MODE_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return default_mode;

    uint8_t val = (uint8_t)default_mode;
    nvs_get_u8(h, NVS_MODE_KEY, &val); /* silently keep default on miss */
    nvs_close(h);
    return (bool)val;
}

/*
 * Persist the chosen mode to NVS so it survives reboots.
 * Called by the monitor use_wifi / use_usb commands.
 */
void platform_mode_save(bool use_serial)
{
    nvs_handle_t h;
    if (nvs_open(NVS_MODE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for mode save");
        return;
    }
    nvs_set_u8(h, NVS_MODE_KEY, (uint8_t)use_serial);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Mode saved to NVS: %s", use_serial ? "USB serial" : "WiFi");
}

/*
 * Determine the GDB interface mode at boot.
 *
 * Priority (highest to lowest):
 *   1. Button held on GPIO39 → USB serial for this boot only (temporary
 *      override, not saved to NVS — useful as an emergency escape hatch).
 *   2. Mode saved in NVS from a previous 'monitor use_wifi/use_usb' call.
 *   3. Default: WiFi (AP mode).
 *
 * Returns: true = USB serial, false = WiFi.
 */
static bool detect_gdb_mode(void)
{
    /* Load the persistently saved mode (default: WiFi). */
    bool saved_mode = mode_nvs_load(false /* default: WiFi */);

    /* Configure the mode-select button as input with pull-up. */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODE_SELECT_BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Saved mode: %s  (hold GPIO%d button to override to USB serial)",
             saved_mode ? "USB serial" : "WiFi", MODE_SELECT_BUTTON_PIN);

    /* Poll the button for up to MODE_SELECT_TIMEOUT_MS. */
    TickType_t start   = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(MODE_SELECT_TIMEOUT_MS);
    bool button_held   = false;

    while ((xTaskGetTickCount() - start) < timeout) {
        if (gpio_get_level(MODE_SELECT_BUTTON_PIN) == 0) {
            button_held = true;
            ESP_LOGI(TAG, "Button held — overriding to USB serial for this boot");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (button_held) {
        /* Temporary override: use serial this boot, do NOT save to NVS.
         * To make serial the permanent default use 'monitor use_usb'. */
        return true;
    }

    return saved_mode;
}

/* Declared in gdb_if_wrapper.c */
extern void gdb_if_wait_connect(void);

/* Serial GDB thread - no WiFi/sockets needed */
static void gdb_serial_thread(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Starting GDB serial interface (USB CDC-ACM)");

    platform_init();

    ESP_LOGI(TAG, "GDB serial ready — waiting for host to open the port");

    while (1) {
        /*
         * Block here until the host opens the CDC-ACM port and sends the
         * first byte.  This is the serial-mode equivalent of accept().
         * Any stale bytes buffered from the previous session are drained
         * inside gdb_if_wait_connect() before it returns.
         */
        gdb_if_wait_connect();

        /*
         * Reset all BMP probe state for the new session.  This clears
         * cur_target, gdb_target_running, noackmode etc. so the new GDB
         * client starts from a clean slate.
         */
        gdb_bmp_state_reset();

        /* Run the GDB session until the host closes the port. */
        main_loop();

        /* Brief yield before listening for the next connection. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#if USE_CUSTOM_DEBUG_UART
/* Custom vprintf that writes to UART1 */
static int uart_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        uart_write_bytes(DEBUG_UART_PORT, buf, len);
    }
    return len;
}

/* Initialize UART1 for debug logs on GPIO26/32 */
static void init_debug_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = DEBUG_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_param_config(DEBUG_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(DEBUG_UART_PORT, DEBUG_UART_TX_PIN, DEBUG_UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(DEBUG_UART_PORT, 256, 256, 0, NULL, 0));

    // Redirect stdout/stderr to UART1
    esp_log_set_vprintf((vprintf_like_t)uart_vprintf);

    ESP_LOGI(TAG, "Debug UART initialized on GPIO%d/GPIO%d @ %d baud",
             DEBUG_UART_TX_PIN, DEBUG_UART_RX_PIN, DEBUG_UART_BAUD);
}
#endif

void app_main()
{
#if USE_CUSTOM_DEBUG_UART
    // Initialize debug UART first, before any logging
    init_debug_uart();
#endif

    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
       ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        printf("no free pages in nvs");
    }

    ESP_ERROR_CHECK( ret );

    // Detect GDB mode via button on GPIO39
    bool use_serial = detect_gdb_mode();
    gdb_if_set_mode(use_serial);

    // Print SWD/JTAG pin configuration
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "SWD/JTAG Pin Configuration:");
    ESP_LOGI(TAG, "  SWDIO (TMS) -> GPIO%d", SWDIO_PIN);
    ESP_LOGI(TAG, "  SWCLK (TCK) -> GPIO%d", SWCLK_PIN);
    ESP_LOGI(TAG, "  NRST        -> GPIO%d", NRST_PIN);
    ESP_LOGI(TAG, "  TDI         -> GPIO%d", TDI_PIN);
    ESP_LOGI(TAG, "  TDO         -> GPIO%d", TDO_PIN);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    if (use_serial) {
        // Serial GDB mode - no WiFi needed
        ESP_LOGI(TAG, "=== GDB SERIAL MODE ===");
        ESP_LOGI(TAG, "UART0 @ 115200 baud");
#if !USE_CUSTOM_DEBUG_UART
        ESP_LOGW(TAG, "WARNING: Debug logs on same UART as GDB!");
        ESP_LOGW(TAG, "Consider setting USE_CUSTOM_DEBUG_UART=1");
#endif

        // Start serial GDB thread directly
        xTaskCreate(&gdb_serial_thread, "gdb_serial", 4*4096, NULL, 17, NULL);

        // Keep app_main running
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } else {
        // WiFi/TCP GDB mode
        ESP_LOGI(TAG, "=== GDB WIFI MODE ===");
#ifndef AP_MODE
        ESP_LOGI(TAG, "Station mode - connecting to existing WiFi");
        initialise_wifi();
#else
        ESP_LOGI(TAG, "Access Point mode - creating WiFi network");
        wifi_init_softap();
#endif

	xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
						false, true, portMAX_DELAY);

	EventBits_t uxBits=xEventGroupWaitBits(wifi_event_group, WIFI_FAIL_BIT,
						false, true, 4000 / portTICK_PERIOD_MS);

    if (( uxBits & WIFI_FAIL_BIT ) != 0) {

        ESP_LOGI(TAG, "Late fail");

        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                            false, true, portMAX_DELAY);

    }

	ESP_LOGI(TAG, "Connected to AP");

	platform_init();

#if PLATFORM_HAS_UART_PASSTHROUGH
	uart_passthrough_init();
#endif

	web_server_init();

    xTaskCreate(&gdb_application_thread, "gdb_thread", 4*4096, NULL, 17, NULL);

   //xTaskCreate(&main_task, "main_task", 4*4096, NULL, 17, NULL);

    // WiFi watchdog - restart on failure
    for(;;) {
        	EventBits_t uxBits=xEventGroupWaitBits(wifi_event_group, WIFI_FAIL_BIT,
						false, true, 20000);

        if (( uxBits & WIFI_FAIL_BIT ) != 0) {
            s_retry_num=0;
            esp_wifi_stop();
            esp_wifi_start();

            uxBits=xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
						false, true, portMAX_DELAY);

            ESP_LOGI(TAG, "Restarted WIFI");

            //platform_init();

           // ESP_ERROR_CHECK(esp_wifi_start() );


        }
    }
    } // End WiFi mode

#if 0
	while (true) {
		volatile struct exception e;
		TRY_CATCH(e, EXCEPTION_ALL) {
			gdb_main();
		}
		if (e.type) {
			gdb_putpacketz("EFF");
			target_list_free();
			morse("TARGET LOST.", 1);
		}
	}
#endif
	/* Should never get here */
	return ;
}

