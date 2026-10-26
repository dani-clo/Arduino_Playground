/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Checks that the ESP8266 on the Nano XL Link Monica carrier answers AT
 * commands: pulses its reset line, sends "AT" and "AT+GMR" at 115200 baud and
 * prints the answer and the decoded versions.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>
#include <sample_usbd.h>

#define ESP_NODE DT_NODELABEL(esp8266)

static const struct device *const esp_uart = DEVICE_DT_GET(DT_BUS(ESP_NODE));
static const struct gpio_dt_spec esp_reset = GPIO_DT_SPEC_GET(ESP_NODE, reset_gpios);
static const struct device *const wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int wdt_channel = -1;

static const struct device *const cdc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define ESP_BAUD 115200
#define RESP_MAX 2048

/* The ISR fills the ring buffer, so nothing is lost while USB output blocks. */
RING_BUF_DECLARE(rx_ring, 4096);
static char resp[RESP_MAX];
static int resp_len;

static void esp_rx_isr(const struct device *dev, void *user_data)
{
	uint8_t buf[32];

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		ring_buf_put(&rx_ring, buf, n);
	}
}

static void feed(void)
{
	if (wdt_channel >= 0) {
		wdt_feed(wdt_dev, wdt_channel);
	}
}

static void watchdog_adopt(void)
{
	/* MCUboot already started the counter: only install the same timeout. */
	const struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = 16000},
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (device_is_ready(wdt_dev)) {
		wdt_channel = wdt_install_timeout(wdt_dev, &cfg);
	}
}

static void sleep_fed(int ms)
{
	while (ms > 0) {
		int step = MIN(ms, 500);

		k_msleep(step);
		feed();
		ms -= step;
	}
}

static void drain(void)
{
	ring_buf_reset(&rx_ring);
}

static void print_escaped(const char *p, int len)
{
	for (int i = 0; i < len; i++) {
		unsigned char c = p[i];

		if (c == '\r') {
			continue;
		}
		if (c == '\n' || (c >= 0x20 && c < 0x7f)) {
			printk("%c", c);
		} else {
			printk("\\x%02x", c);
		}
	}
}

/* Collects for timeout_ms, then prints it. Returns true if "OK" was seen. */
static bool collect_print(int timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	resp_len = 0;
	while (k_uptime_get() < end) {
		resp_len += ring_buf_get(&rx_ring, (uint8_t *)resp + resp_len,
					 RESP_MAX - 1 - resp_len);
		if (resp_len >= RESP_MAX - 1) {
			break;
		}
		k_msleep(5);
		feed();
	}
	resp_len += ring_buf_get(&rx_ring, (uint8_t *)resp + resp_len, RESP_MAX - 1 - resp_len);
	resp[resp_len] = '\0';
	print_escaped(resp, resp_len);
	printk("\n");
	return strstr(resp, "OK") != NULL;
}

static bool send_cmd(const char *cmd, int timeout_ms)
{
	drain();
	printk(">> %s\n", cmd);
	for (const char *p = cmd; *p; p++) {
		uart_poll_out(esp_uart, *p);
	}
	uart_poll_out(esp_uart, '\r');
	uart_poll_out(esp_uart, '\n');
	return collect_print(timeout_ms);
}

/* Prints the value after "key" up to the end of the line, if present. */
static void print_field(const char *label, const char *key)
{
	const char *p = strstr(resp, key);

	if (p == NULL) {
		printk("  %-12s (not found)\n", label);
		return;
	}
	p += strlen(key);
	printk("  %-12s ", label);
	while (*p && *p != '\r' && *p != '\n') {
		printk("%c", *p++);
	}
	printk("\n");
}

static void reset_esp(void)
{
	gpio_pin_configure_dt(&esp_reset, GPIO_OUTPUT_INACTIVE);
	gpio_pin_set_dt(&esp_reset, 1);
	sleep_fed(100);
	gpio_pin_set_dt(&esp_reset, 0);
}

/* Opening the port at 1200 baud reboots into MCUboot's DFU mode, like the core. */
static void reboot_dfu_work_fn(struct k_work *work)
{
	bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(reboot_dfu_work, reboot_dfu_work_fn);

static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx) && msg->type == USBD_MSG_VBUS_READY) {
		usbd_enable(ctx);
	}

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		uint32_t baud;

		uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_BAUD_RATE, &baud);
		if (baud == 1200) {
			k_work_schedule(&reboot_dfu_work, K_MSEC(100));
		}
	}
}

/* Starts USB CDC and waits (up to 30 s) for a terminal to open the port. */
static void usb_console_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(usb_msg_cb);

	if (usbd == NULL || (!usbd_can_detect_vbus(usbd) && usbd_enable(usbd))) {
		return;
	}

	for (int i = 0; i < 300; i++) {
		uint32_t dtr = 0;

		uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			break;
		}
		k_msleep(100);
		feed();
	}
	k_msleep(200);
}

int main(void)
{
	watchdog_adopt();
	usb_console_start();

	printk("\n*** Monica ESP8266 AT test ***\n");

	if (!device_is_ready(esp_uart) || !gpio_is_ready_dt(&esp_reset)) {
		printk("ESP UART or reset GPIO not ready\n");
		return 0;
	}

	struct uart_config cfg = {
		.baudrate = ESP_BAUD,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int ret = uart_configure(esp_uart, &cfg);

	printk("UART at %d baud (uart_configure=%d)\n", ESP_BAUD, ret);
	uart_irq_callback_user_data_set(esp_uart, esp_rx_isr, NULL);
	uart_irq_rx_enable(esp_uart);

	while (1) {
		bool ok = false;

		reset_esp();
		/* The ROM prints at 74880 baud, so only the final "ready" is readable. */
		printk("\n[boot output]\n");
		collect_print(3000);

		for (int try = 0; try < 3 && !ok; try++) {
			ok = send_cmd("AT", 1000);
		}

		if (ok) {
			send_cmd("ATE0", 500);
			if (send_cmd("AT+GMR", 1500)) {
				printk("\nVersions:\n");
				print_field("AT version", "AT version:");
				print_field("SDK version", "SDK version:");
				print_field("Compile time", "compile time:");
				print_field("Bin version", "Bin version");
			}
			send_cmd("AT+CWMODE?", 1000);
			printk("\nRESULT: ESP8266 answered at %d baud\n", ESP_BAUD);
		} else {
			printk("\nRESULT: no AT answer at %d baud\n", ESP_BAUD);
		}
		printk("Retrying in 10 s\n");
		sleep_fed(10000);
	}
	return 0;
}
