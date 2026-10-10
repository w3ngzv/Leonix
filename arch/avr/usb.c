// SPDX-License-Identifier: GPL-2.0-only
/*
 * USB device controller, presenting the board as a CDC-ACM serial port.
 *
 * The host enumerates the device through endpoint 0 and then opens the
 * port.  Endpoint 1 is the CDC notification endpoint, which the class
 * requires but which never sends; endpoint 2 receives what the host
 * writes and endpoint 3 is the way out.  What arrives on endpoint 2 is
 * dropped, so that a host writing to the port does not stall.
 *
 * The port is the kernel log's console.  While the host holds DTR, which
 * a terminal program raises on open, each printk() record goes out as
 * one line.  Opening the port first replays whatever records the log
 * still holds.
 *
 * Closing the port after setting it to 1200 baud restarts the board into
 * Caterina, which then stays for the programmer: the Arduino core's
 * convention, so that flashing needs no press of the reset button.  See
 * Documentation/booting.rst, section 3.
 *
 * The endpoint interrupt only masks itself and wakes usb_task(), which
 * serves the request, the way the TWI interrupt hands an I2C transfer
 * back to its caller.  A control transfer polls the endpoint between
 * packets for up to USB_EP0_TIMEOUT_MS, and doing that in the interrupt
 * would hold interrupts off for as long.  It would also run on whichever
 * task's stack was interrupted: the idle task has 13 bytes to spare.
 *
 * Both vectors are in switch.S and use the tick's frame, so their stack
 * use is covered by TASK_STACK_RESERVE as long as the handlers here stay
 * shallower than schedule().
 *
 * Register sequences follow the ATmega32U4 datasheet, 7766J, chapters
 * 21 and 22; requests and descriptors follow USB 2.0 chapter 9 and the
 * CDC PSTN subclass 1.2.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/usb.h>
#include <leonix/wait.h>

#define EP0_SIZE		64
#define CDC_NOTIFY_EP		1
#define CDC_RX_EP		2
#define CDC_TX_EP		3
#define CDC_NOTIFY_SIZE		16
#define CDC_DATA_SIZE		64

/*
 * Arduino's IDs for a Leonardo running a sketch.  Caterina enumerates
 * as 0x0036; the host tells the two apart by the product ID.
 */
#define USB_VID			0x2341
#define USB_PID			0x8036

/* Datasheet 6.11.5: the PLL takes "several ms" to lock. */
#define USB_PLL_TIMEOUT_MS	10
#define USB_EP0_TIMEOUT_MS	10
#define USB_SPIN_US		10

/*
 * avr-gcc 9.5.0 -fstack-usage puts the deepest path at console_flush()
 * formatting a number: usb_task() 16, console_flush() 66, snprintk_P()
 * 4, vsnprintk_P() 22, put_number() 34, put() 2, 144 bytes in all.  The
 * switch frame and the canary bring it to 201.
 */
#define USB_TASK_STACK_SIZE	224

/* bmRequestType */
#define REQ_TYPE_MASK		0x60
#define REQ_TYPE_STANDARD	0x00
#define REQ_TYPE_CLASS		0x20

/* USB 2.0 table 9-4 */
#define REQ_GET_STATUS		0
#define REQ_CLEAR_FEATURE	1
#define REQ_SET_FEATURE		3
#define REQ_SET_ADDRESS		5
#define REQ_GET_DESCRIPTOR	6
#define REQ_GET_CONFIGURATION	8
#define REQ_SET_CONFIGURATION	9

/* CDC PSTN 1.2 table 13 */
#define CDC_SET_LINE_CODING		0x20
#define CDC_GET_LINE_CODING		0x21
#define CDC_SET_CONTROL_LINE_STATE	0x22

/* USB 2.0 table 9-5 */
#define DESC_DEVICE		1
#define DESC_CONFIGURATION	2
#define DESC_STRING		3
#define DESC_INTERFACE		4
#define DESC_ENDPOINT		5
#define DESC_CS_INTERFACE	0x24	/* CDC 1.2 table 12 */

#define USB_CONFIG_VALUE	1
#define USB_MAX_POWER_MA	100
#define EP_IN			0x80
#define EP_BULK			2
#define EP_INTERRUPT		3
#define CDC_NOTIFY_INTERVAL_MS	64
#define LANG_EN_US		0x0409

/* CDC PSTN 1.2 table 18: DTR, set while the host has the port open. */
#define CDC_LINE_DTR		(1 << 0)

/*
 * Caterina stays in the bootloader after a watchdog reset if this word
 * holds the key.  The address is in the boot stack, which nothing uses
 * after sched_start(); the Makefile checks that .bss ends below it.
 */
#define BOOT_KEY_ADDR		0x0800
#define BOOT_KEY		0x7777
#define BOOTLOADER_BAUD		1200

/* "[secs] text\r\n", one console line. */
#define CONSOLE_LINE		(LOG_TEXT + 12)

/* Events from the endpoint interrupt to usb_task(). */
#define USB_EV_SETUP		(1 << 0)
#define USB_EV_RX		(1 << 1)

#define lo(w)	((w) & 0xff)
#define hi(w)	((w) >> 8)

struct setup_packet {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};

/* CDC PSTN 1.2 table 17, as the host last set it. */
struct line_coding {
	uint32_t baud;
	uint8_t stop_bits;
	uint8_t parity;
	uint8_t data_bits;
};

static const uint8_t device_desc[] PROGMEM = {
	18, DESC_DEVICE, lo(0x0200), hi(0x0200),
	0x02, 0x00, 0x00,		/* class CDC, at device level */
	EP0_SIZE,
	lo(USB_VID), hi(USB_VID), lo(USB_PID), hi(USB_PID),
	lo(0x0100), hi(0x0100),		/* bcdDevice */
	0, 1, 0,			/* no manufacturer, product, no serial */
	1,
};

#define CONFIG_DESC_LEN	67

static const uint8_t config_desc[CONFIG_DESC_LEN] PROGMEM = {
	9, DESC_CONFIGURATION, lo(CONFIG_DESC_LEN), hi(CONFIG_DESC_LEN),
	2, USB_CONFIG_VALUE, 0, 0x80, USB_MAX_POWER_MA / 2,

	/* Interface 0, communication class, abstract control model */
	9, DESC_INTERFACE, 0, 0, 1, 0x02, 0x02, 0x01, 0,
	5, DESC_CS_INTERFACE, 0x00, lo(0x0110), hi(0x0110),	/* header */
	5, DESC_CS_INTERFACE, 0x01, 0x00, 1,	/* call management */
	4, DESC_CS_INTERFACE, 0x02, 0x02,	/* ACM: line coding, line state */
	5, DESC_CS_INTERFACE, 0x06, 0, 1,	/* union: 0 controls 1 */
	7, DESC_ENDPOINT, EP_IN | CDC_NOTIFY_EP, EP_INTERRUPT,
	CDC_NOTIFY_SIZE, 0, CDC_NOTIFY_INTERVAL_MS,

	/* Interface 1, data class */
	9, DESC_INTERFACE, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
	7, DESC_ENDPOINT, CDC_RX_EP, EP_BULK, CDC_DATA_SIZE, 0, 0,
	7, DESC_ENDPOINT, EP_IN | CDC_TX_EP, EP_BULK, CDC_DATA_SIZE, 0, 0,
};

static const uint8_t lang_desc[] PROGMEM = {
	4, DESC_STRING, lo(LANG_EN_US), hi(LANG_EN_US),
};

static const uint8_t product_desc[] PROGMEM = {
	14, DESC_STRING, 'L', 0, 'e', 0, 'o', 0, 'n', 0, 'i', 0, 'x', 0,
};

static uint8_t usb_task_stack[USB_TASK_STACK_SIZE];
static struct wait_queue usb_wq = WAIT_QUEUE_INIT;
static volatile uint8_t usb_events;

static uint8_t usb_config;
static uint16_t line_state;
static uint8_t console_replay;	/* DTR has just risen */
static uint16_t console_sent;	/* last record sent */
static struct line_coding line = { 9600, 0, 0, 8 };

/*
 * Wait for any flag in @mask on the selected endpoint.  Returns the
 * flags found, or 0 once USB_EP0_TIMEOUT_MS has passed.  The count is
 * of spins, so time spent switched out only makes the wait longer.
 */
static uint8_t ep_wait(uint8_t mask)
{
	uint16_t spins = USB_EP0_TIMEOUT_MS * 1000UL / USB_SPIN_US;
	uint8_t flags;

	while (!(flags = UEINTX & mask)) {
		if (!spins--)
			return 0;
		_delay_us(USB_SPIN_US);
	}
	return flags;
}

/* Status stage of a request without data, or after an OUT data stage. */
static void ep0_ack(void)
{
	if (ep_wait(1 << TXINI))
		UEINTX = ~(1 << TXINI);
}

static void ep0_stall(void)
{
	UECONX = (1 << STALLRQ) | (1 << EPEN);
}

/*
 * Data stage of a control read: @len bytes from @p, cut to what the
 * host asked for, in EP0_SIZE packets, then the status OUT.  A reply
 * shorter than the request that ends on a full packet needs a
 * zero-length packet after it, USB 2.0 5.5.3.  The host may end the
 * data stage early with the status OUT, datasheet 22.12.2.
 */
static void ep0_send(const uint8_t *p, uint8_t len, uint16_t requested,
		     uint8_t in_flash)
{
	uint8_t zlp, n, i, flags;

	if (len > requested)
		len = requested;
	zlp = len < requested;
	for (;;) {
		flags = ep_wait((1 << TXINI) | (1 << RXOUTI));
		if (!flags)
			return;
		if (flags & (1 << RXOUTI))
			break;
		n = len < EP0_SIZE ? len : EP0_SIZE;
		len -= n;
		for (i = 0; i < n; i++) {
			UEDATX = in_flash ? pgm_read_byte(p) : *p;
			p++;
		}
		UEINTX = ~(1 << TXINI);
		if (!len && (n < EP0_SIZE || !zlp)) {
			if (!ep_wait(1 << RXOUTI))
				return;
			break;
		}
	}
	UEINTX = ~(1 << RXOUTI);
}

/* Datasheet 22.6.  Returns non-zero if the controller took the layout. */
static uint8_t ep_configure(uint8_t ep, uint8_t cfg0, uint8_t cfg1)
{
	UENUM = ep;
	UECONX = 1 << EPEN;
	UECFG0X = cfg0;
	UECFG1X = cfg1;
	return UESTA0X & (1 << CFGOK);
}

/*
 * DPRAM is allocated in endpoint order, datasheet 21.9, so the CDC
 * endpoints are configured from the lowest up.  The data IN endpoint has
 * two banks, so one can fill while the other is on the bus.
 */
static void cdc_configure(void)
{
	ep_configure(CDC_NOTIFY_EP,
		     (1 << EPTYPE1) | (1 << EPTYPE0) | (1 << EPDIR),
		     (1 << EPSIZE0) | (1 << ALLOC));
	ep_configure(CDC_RX_EP, 1 << EPTYPE1,
		     (1 << EPSIZE1) | (1 << EPSIZE0) | (1 << ALLOC));
	UEIENX = 1 << RXOUTE;
	ep_configure(CDC_TX_EP, (1 << EPTYPE1) | (1 << EPDIR),
		     (1 << EPSIZE1) | (1 << EPSIZE0) | (1 << EPBK0) |
		     (1 << ALLOC));
	UERST = (1 << CDC_NOTIFY_EP) | (1 << CDC_RX_EP) | (1 << CDC_TX_EP);
	UERST = 0;
	UENUM = 0;
}

static void get_descriptor(const struct setup_packet *setup)
{
	const uint8_t *desc;
	uint8_t len;

	switch (hi(setup->value)) {
	case DESC_DEVICE:
		desc = device_desc;
		len = sizeof(device_desc);
		break;
	case DESC_CONFIGURATION:
		desc = config_desc;
		len = sizeof(config_desc);
		break;
	case DESC_STRING:
		if (lo(setup->value) == 0) {
			desc = lang_desc;
			len = sizeof(lang_desc);
		} else if (lo(setup->value) == 1) {
			desc = product_desc;
			len = sizeof(product_desc);
		} else {
			ep0_stall();
			return;
		}
		break;
	default:
		/* Including DEVICE_QUALIFIER: a full-speed-only device stalls. */
		ep0_stall();
		return;
	}
	ep0_send(desc, len, setup->length, 1);
}

static void standard_request(const struct setup_packet *setup)
{
	static const uint8_t zero_status[2];

	switch (setup->request) {
	case REQ_GET_STATUS:
		ep0_send(zero_status, sizeof(zero_status), setup->length, 0);
		break;
	case REQ_CLEAR_FEATURE:
	case REQ_SET_FEATURE:
		/* No remote wakeup, and no endpoint is ever halted. */
		ep0_ack();
		break;
	case REQ_SET_ADDRESS:
		/* Datasheet 22.7: UADD first, ADDEN after the status stage. */
		UDADDR = lo(setup->value) & 0x7f;
		ep0_ack();
		if (ep_wait(1 << TXINI))
			UDADDR |= 1 << ADDEN;
		break;
	case REQ_GET_DESCRIPTOR:
		get_descriptor(setup);
		break;
	case REQ_GET_CONFIGURATION:
		ep0_send(&usb_config, 1, setup->length, 0);
		break;
	case REQ_SET_CONFIGURATION:
		if (lo(setup->value) > USB_CONFIG_VALUE) {
			ep0_stall();
			break;
		}
		usb_config = lo(setup->value);
		if (usb_config)
			cdc_configure();
		ep0_ack();
		printk("usb config %u", usb_config);
		break;
	default:
		ep0_stall();
		break;
	}
}

/*
 * The watchdog fires after the status stage has gone out, and the host
 * sees the device leave the bus.  Tasks keep running until then.
 */
static void enter_bootloader(void)
{
	*(volatile uint16_t *)BOOT_KEY_ADDR = BOOT_KEY;
	wdt_enable(WDTO_120MS);
}

static void class_request(const struct setup_packet *setup)
{
	uint8_t *p = (uint8_t *)&line;
	uint8_t n;

	switch (setup->request) {
	case CDC_SET_LINE_CODING:
		if (!ep_wait(1 << RXOUTI))
			break;
		for (n = 0; n < sizeof(line); n++)
			*p++ = UEDATX;
		UEINTX = ~(1 << RXOUTI);
		ep0_ack();
		break;
	case CDC_GET_LINE_CODING:
		ep0_send(p, sizeof(line), setup->length, 0);
		break;
	case CDC_SET_CONTROL_LINE_STATE:
		if (setup->value & ~line_state & CDC_LINE_DTR)
			console_replay = 1;
		line_state = setup->value;
		ep0_ack();
		if (line.baud == BOOTLOADER_BAUD && !(line_state & CDC_LINE_DTR))
			enter_bootloader();
		break;
	default:
		ep0_stall();
		break;
	}
}

static void ep0_setup(void)
{
	struct setup_packet setup;
	uint8_t *p = (uint8_t *)&setup;
	uint8_t n;

	UENUM = 0;
	if (!(UEINTX & (1 << RXSTPI)))
		return;
	for (n = 0; n < sizeof(setup); n++)
		*p++ = UEDATX;
	/* Datasheet 22.12.2: a SETUP overrides whatever was in progress. */
	UEINTX = ~((1 << RXSTPI) | (1 << RXOUTI) | (1 << TXINI));

	if ((setup.request_type & REQ_TYPE_MASK) == REQ_TYPE_STANDARD)
		standard_request(&setup);
	else if ((setup.request_type & REQ_TYPE_MASK) == REQ_TYPE_CLASS)
		class_request(&setup);
	else
		ep0_stall();
}

/* What the host writes to the port is dropped, bank by bank. */
static void cdc_rx(void)
{
	UENUM = CDC_RX_EP;
	if (!(UEINTX & (1 << RXOUTI)))
		return;
	UEINTX = ~(1 << RXOUTI);
	UEINTX = (uint8_t)~(1 << FIFOCON);
}

/*
 * Send @len bytes as one packet on the data IN endpoint.  Returns -1 if
 * the bank stays full, which happens when nothing on the host reads the
 * port.  @len is at most CDC_DATA_SIZE.
 */
static int cdc_write(const char *s, uint8_t len)
{
	int ret = -1;

	UENUM = CDC_TX_EP;
	if (ep_wait(1 << TXINI)) {
		UEINTX = ~(1 << TXINI);
		while (len--)
			UEDATX = *s++;
		UEINTX = (uint8_t)~(1 << FIFOCON);
		ret = 0;
	}
	UENUM = 0;
	return ret;
}

static uint8_t console_open(void)
{
	return usb_config && (line_state & CDC_LINE_DTR);
}

/*
 * Send the records after console_sent.  On open, start from the oldest
 * record still in the log and say how many came before it.  A record
 * overwritten before it could be sent is counted the same way.  If the
 * host stops reading, the rest are dropped rather than retried, so that
 * a stalled port costs one timeout per record and no more.
 *
 * Kept out of line, so that its line buffer and record are not in
 * usb_task()'s frame under the printk() in standard_request().
 */
static void __attribute__((noinline)) console_flush(void)
{
	struct log_record rec;
	char line[CONSOLE_LINE];
	uint16_t newest = log_newest();
	uint16_t lost = 0;
	uint8_t len;

	if (console_replay) {
		console_replay = 0;
		console_sent = newest > LOG_RECORDS ? newest - LOG_RECORDS : 0;
		lost = console_sent;
	}
	while (console_sent != newest) {
		if (log_read(++console_sent, &rec) < 0) {
			lost++;
			continue;
		}
		if (lost) {
			len = snprintk(line, sizeof(line), "(%u lost)\r\n", lost);
			lost = 0;
			if (cdc_write(line, len) < 0)
				break;
		}
		len = snprintk(line, sizeof(line), "[%5u] %s\r\n", rec.secs,
			       rec.text);
		if (cdc_write(line, len) < 0)
			break;
	}
	if (lost) {
		len = snprintk(line, sizeof(line), "(%u lost)\r\n", lost);
		cdc_write(line, len);
	}
	console_sent = newest;
}

/* Taken in wait_event(), with interrupts off. */
static uint8_t usb_take_events(void)
{
	uint8_t events = usb_events;

	usb_events = 0;
	return events;
}

static uint8_t console_pending(void)
{
	return console_open() && (console_replay || console_sent != log_newest());
}

/* The console kick from printk(), possibly in an interrupt. */
static void usb_console_kick(void)
{
	wake_up(&usb_wq);
}

/*
 * Serves what the endpoint interrupt reported, then unmasks it.  A
 * SETUP is looked for whenever the task wakes, since a bus reset in the
 * middle of a request leaves endpoint 0 unmasked again.
 */
static void usb_task(void)
{
	uint8_t events;

	for (;;) {
		wait_event(&usb_wq,
			   (events = usb_take_events()) || console_pending());
		if (events & USB_EV_SETUP) {
			ep0_setup();
			UENUM = 0;
			UEIENX = 1 << RXSTPE;
		}
		if (events & USB_EV_RX) {
			cdc_rx();
			UEIENX = 1 << RXOUTE;
		}
		UENUM = 0;
		if (console_open())
			console_flush();
	}
}

/*
 * End of a bus reset: every endpoint but 0 is gone, datasheet 22.4.
 * Called from switch.S with interrupts off; never wakes a task.
 */
uint8_t usb_general_interrupt(void)
{
	uint8_t ep_saved = UENUM;

	if (UDINT & (1 << EORSTI)) {
		UDINT = ~(1 << EORSTI);
		usb_config = 0;
		ep_configure(0, 0, (1 << EPSIZE1) | (1 << EPSIZE0) | (1 << ALLOC));
		UEIENX = 1 << RXSTPE;
	}
	UENUM = ep_saved;
	return 0;
}

/*
 * Masks the endpoint that raised the interrupt and hands it to
 * usb_task().  usb_task() may be in the middle of using UENUM, so the
 * selection is put back.  Returns the number of tasks woken, for
 * switch.S.
 */
uint8_t usb_endpoint_interrupt(void)
{
	uint8_t ep_saved = UENUM;
	uint8_t pending = UEINT;

	if (pending & (1 << 0)) {
		UENUM = 0;
		UEIENX = 0;
		usb_events |= USB_EV_SETUP;
	}
	if (pending & (1 << CDC_RX_EP)) {
		UENUM = CDC_RX_EP;
		UEIENX = 0;
		usb_events |= USB_EV_RX;
	}
	UENUM = ep_saved;
	return wake_up(&usb_wq);
}

/*
 * Datasheet 21.12, "Power On the USB interface".  The board then attaches
 * on its own whenever VBUS is present, datasheet 22.2.  Called before
 * sched_start(), with interrupts off.  Returns -1, with the controller
 * left off, if the PLL does not lock or the task cannot be created.
 */
int usb_init(void)
{
	uint16_t spins = USB_PLL_TIMEOUT_MS * 1000UL / USB_SPIN_US;

	if (task_create(usb_task, usb_task_stack, sizeof(usb_task_stack)) < 0)
		return -1;
	register_console(usb_console_kick);

	UHWCON = 1 << UVREGE;
	USBCON = (1 << USBE) | (1 << FRZCLK);
	PLLFRQ = 1 << PDIV2;			/* 48 MHz, straight to USB */
	PLLCSR = (1 << PINDIV) | (1 << PLLE);	/* 16 MHz crystal, halved */
	while (!(PLLCSR & (1 << PLOCK))) {
		if (!spins--) {
			PLLCSR = 0;
			USBCON = 1 << FRZCLK;
			UHWCON = 0;
			return -1;
		}
		_delay_us(USB_SPIN_US);
	}
	USBCON = (1 << USBE) | (1 << OTGPADE);
	UDCON = 0;				/* full speed, attach */
	UDIEN = 1 << EORSTE;
	return 0;
}
