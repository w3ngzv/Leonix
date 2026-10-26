// SPDX-License-Identifier: GPL-2.0-only
/*
 * USB device controller, presenting the board as a CDC-ACM serial port.
 *
 * The host enumerates the device through endpoint 0 and then opens the
 * port.  Endpoint 1 is the CDC notification endpoint, which the class
 * requires but which never sends; endpoint 2 receives what the host
 * writes and endpoint 3 is the way out.
 *
 * The data endpoints carry the terminal ttyACM0, see drivers/tty.c,
 * which reads and writes them through acm_fops.  Bytes from the
 * host wait in acm_rx_buf for acm_read(); a bank that does not fit is
 * left in the controller, which then NAKs the host until a reader makes
 * room.  acm_write() queues bytes in acm_tx_buf for usb_task() to send.
 * Only usb_task() touches the endpoints, since UENUM is one selection
 * shared by everyone.
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
 *
 * USB 设备控制器, 把板子呈现为 CDC-ACM 串口.
 *
 * 主机经由端点 0 枚举设备, 然后打开串口. 端点 1 是 CDC 通知端点, 该类要求其存
 * 在, 但从不发送; 端点 2 接收主机写入的数据, 端点 3 负责发出.
 *
 * 数据端点承载终端 ttyACM0 (见 drivers/tty.c), 终端经由 acm_fops 读写数据端点.
 * 来自主机的字节在 acm_rx_buf 中等待 acm_read(); 放不下的数据块留在控制器中, 控
 * 制器随后对主机回 NAK, 直到读者腾出空间. acm_write() 把字节放入 acm_tx_buf,
 * 由 usb_task() 发送. 只有 usb_task() 访问端点, 因为 UENUM 是所有代码共用的一个
 * 选择.
 *
 * 该串口是内核日志的控制台. 主机保持 DTR (终端程序打开串口时置位) 期间, 每条
 * printk() 记录作为一行发出. 打开串口时先重放日志中仍保留的记录.
 *
 * 把串口设为 1200 波特后关闭, 板子重启进入 Caterina, 并停留在其中等待烧录器: 这
 * 是 Arduino core 的约定, 烧录因此无需按复位键. 见 Documentation/booting.rst 第
 * 3 节.
 *
 * 端点中断只屏蔽自身并唤醒 usb_task(), 由 usb_task() 处理请求, 与 TWI 中断把
 * I2C 传输交还给调用者的方式相同. 一次控制传输在数据包之间轮询端点, 最长
 * USB_EP0_TIMEOUT_MS; 若在中断中进行, 中断会被关闭同样长的时间. 而且这会运行在
 * 被中断的那个任务的栈上: 空闲任务只余 13 字节.
 *
 * 两个中断向量都在 switch.S 中, 使用与节拍中断相同的栈帧, 因此只要此处的处理函
 * 数比 schedule() 浅, 其栈用量就由 TASK_STACK_RESERVE 覆盖.
 *
 * 寄存器操作顺序遵循 ATmega32U4 数据手册 7766J 第 21、22 章; 请求与描述符遵循
 * USB 2.0 第 9 章与 CDC PSTN 子类 1.2.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include <util/delay.h>

#include <leonix/fs.h>
#include <leonix/memlayout.h>
#include <leonix/tty.h>
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
 *
 * Arduino 为运行 sketch 的 Leonardo 分配的 ID. Caterina 以 0x0036 枚举, 主机凭
 * 产品 ID 区分两者.
 */
#define USB_VID			0x2341
#define USB_PID			0x8036

/*
 * datasheet 6.11.5: the PLL takes "several ms" to lock
 * 数据手册 6.11.5: PLL 锁定需要 "several ms"
 */
#define USB_PLL_TIMEOUT_MS	10
#define USB_EP0_TIMEOUT_MS	10
#define USB_SPIN_US		10

/*
 * avr-gcc 9.5.0 -fstack-usage puts the deepest path at console_flush()
 * formatting a number: usb_task() 16, console_flush() 66, snprintk_P()
 * 4, vsnprintk_P() 22, put_number() 34, put() 2, 144 bytes in all.  The
 * switch frame and the canary bring it to 201.
 *
 * avr-gcc 9.5.0 -fstack-usage 给出的最深路径是 console_flush() 格式化数字:
 * usb_task() 16, console_flush() 66, snprintk_P() 4, vsnprintk_P() 22,
 * put_number() 34, put() 2, 共 144 字节. 加上切换栈帧与金丝雀为 201 字节.
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

#define USB_CONF_VALUE	1
#define USB_MAX_POWER_MA	100
#define EP_IN			0x80
#define EP_BULK			2
#define EP_INTERRUPT		3
#define CDC_NOTIFY_INTERVAL_MS	64
#define LANG_EN_US		0x0409

/*
 * CDC PSTN 1.2 table 18: DTR, set while the host has the port open
 * CDC PSTN 1.2 表 18: DTR, 主机打开串口期间置位
 */
#define CDC_LINE_DTR		(1 << 0)

/*
 * Caterina stays in the bootloader after a watchdog reset if the word at
 * BOOT_KEY_ADDR holds the key.  include/leonix/memlayout.h keeps
 * everything else off that word.
 *
 * 看门狗复位后, 若 BOOT_KEY_ADDR 处的字为该键值, Caterina 停留在引导程序中.
 * include/leonix/memlayout.h 使其他数据都不占用该字.
 */
#define BOOT_KEY		0x7777
#define BOOTLOADER_BAUD		1200

/*
 * "[secs] text\r\n", one console line
 * 一行控制台输出, 格式为 "[秒] 文本\r\n"
 */
#define CONSOLE_LINE		(LOG_TEXT + 12)

/*
 * events from the endpoint interrupt to usb_task()
 * 端点中断传给 usb_task() 的事件
 */
#define USB_EV_SETUP		(1 << 0)
#define USB_EV_RX		(1 << 1)

/*
 * Ring sizes, powers of two so that the free-running 8-bit indices wrap
 * with them.  One producer and one consumer each, and an 8-bit index is
 * read and written in one instruction, so the rings need no lock.
 *
 * 环形缓冲区的大小取 2 的幂, 使自由增长的 8 位下标随之回绕. 每个环只有一个生产
 * 者与一个消费者, 且 8 位下标以一条指令读写, 因此不需要锁.
 */
#define ACM_RX_SIZE		32
#define ACM_TX_SIZE		64

/*
 * A packet as long as wMaxPacketSize does not end a bulk transfer on its
 * own, USB 2.0 5.8.3; the host may hold it until a short one follows.
 * Sending at most one byte less makes every packet short.
 *
 * 长度等于 wMaxPacketSize 的包本身不结束批量传输 (USB 2.0 5.8.3), 主机可能一直
 * 等到短包到来. 每次至多少发一个字节, 每个包就都是短包.
 */
#define ACM_TX_PACKET		(CDC_DATA_SIZE - 1)

_Static_assert(256 % ACM_RX_SIZE == 0 && 256 % ACM_TX_SIZE == 0,
	       "ring sizes must divide the 8-bit index range");

#define lo(w)	((w) & 0xff)
#define hi(w)	((w) >> 8)

struct setup_packet {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};

/*
 * CDC PSTN 1.2 table 17, as the host last set it
 * CDC PSTN 1.2 表 17, 取主机最后一次设置的值
 */
struct line_coding {
	uint32_t baud;
	uint8_t stop_bits;
	uint8_t parity;
	uint8_t data_bits;
};

static const uint8_t device_desc[] PROGMEM = {
	18, DESC_DEVICE, lo(0x0200), hi(0x0200),
	/* class CDC, at device level; 在设备级声明 CDC 类 */
	0x02, 0x00, 0x00,
	EP0_SIZE,
	lo(USB_VID), hi(USB_VID), lo(USB_PID), hi(USB_PID),
	lo(0x0100), hi(0x0100),		/* bcdDevice */
	/*
	 * no manufacturer string, product string 1, no serial number
	 * 没有厂商字符串, 产品字符串为 1, 没有序列号
	 */
	0, 1, 0,
	1,
};

#define CONF_DESC_LEN	67

static const uint8_t config_desc[CONF_DESC_LEN] PROGMEM = {
	9, DESC_CONFIGURATION, lo(CONF_DESC_LEN), hi(CONF_DESC_LEN),
	2, USB_CONF_VALUE, 0, 0x80, USB_MAX_POWER_MA / 2,

	/*
	 * interface 0, communication class, abstract control model
	 * 接口 0, 通信类, 抽象控制模型
	 */
	9, DESC_INTERFACE, 0, 0, 1, 0x02, 0x02, 0x01, 0,
	5, DESC_CS_INTERFACE, 0x00, lo(0x0110), hi(0x0110),	/* header */
	5, DESC_CS_INTERFACE, 0x01, 0x00, 1,	/* call management */
	4, DESC_CS_INTERFACE, 0x02, 0x02,	/* ACM: line coding, line state */
	/* union: 0 controls 1; union: 接口 0 控制接口 1 */
	5, DESC_CS_INTERFACE, 0x06, 0, 1,
	7, DESC_ENDPOINT, EP_IN | CDC_NOTIFY_EP, EP_INTERRUPT,
	CDC_NOTIFY_SIZE, 0, CDC_NOTIFY_INTERVAL_MS,

	/*
	 * interface 1, data class
	 * 接口 1, 数据类
	 */
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

static uint8_t usb_task_stack[USB_TASK_STACK_SIZE] __task_stack;
static struct wait_queue usb_wq = WAIT_QUEUE_INIT;
static volatile uint8_t usb_events;

static uint8_t usb_config;
static uint16_t line_state;
/*
 * console_replay: DTR has just risen.  console_sent: the last record
 * sent.
 *
 * console_replay: DTR 刚刚置位. console_sent: 最后发出的记录.
 */
static uint8_t console_replay;
static uint16_t console_sent;
static struct line_coding line = { 9600, 0, 0, 8 };

static char acm_rx_buf[ACM_RX_SIZE];
static volatile uint8_t acm_rx_head, acm_rx_tail;
/*
 * a bank waits for room
 * 有数据块在等待空间
 */
static volatile uint8_t acm_rx_held;
static struct wait_queue acm_rx_wq = WAIT_QUEUE_INIT;

static char acm_tx_buf[ACM_TX_SIZE];
static volatile uint8_t acm_tx_head, acm_tx_tail;
static struct wait_queue acm_tx_wq = WAIT_QUEUE_INIT;

/*
 * Wait for any flag in @mask on the selected endpoint.  Returns the
 * flags found, or 0 once USB_EP0_TIMEOUT_MS has passed.  The count is
 * of spins, so time spent switched out only makes the wait longer.
 *
 * 等待当前选中端点上 @mask 中的任一标志. 返回找到的标志; 超过
 * USB_EP0_TIMEOUT_MS 后返回 0. 计的是自旋次数, 因此被切换出去的时间只会使等待变
 * 长.
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

/*
 * status stage of a request without data, or after an OUT data stage
 * 无数据请求的状态阶段, 或 OUT 数据阶段之后的状态阶段
 */
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
 *
 * 控制读的数据阶段: 发送 @p 起的 @len 字节, 截断到主机请求的长度, 分为 EP0_SIZE
 * 大小的包, 然后是状态 OUT. 应答比请求短且以满包结束时, 其后需要一个零长度包
 * (USB 2.0 5.5.3). 主机可以用状态 OUT 提前结束数据阶段 (数据手册 22.12.2).
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

/*
 * datasheet 22.6; non-zero if the controller took the layout
 * 数据手册 22.6; 控制器接受该配置时返回非零值
 */
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
 *
 * DPRAM 按端点顺序分配 (数据手册 21.9), 因此 CDC 端点从编号最小的开始配置. 数据
 * IN 端点有两个数据块, 一个在总线上传输时另一个可以填充.
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
	acm_rx_held = 0;
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
		/*
		 * including DEVICE_QUALIFIER: a full-speed-only device stalls
		 * 包括 DEVICE_QUALIFIER: 只支持全速的设备对其回 STALL
		 */
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
		/*
		 * no remote wakeup, and no endpoint is ever halted
		 * 不支持远程唤醒, 也从不停止任何端点
		 */
		ep0_ack();
		break;
	case REQ_SET_ADDRESS:
		/*
		 * datasheet 22.7: UADD first, ADDEN after the status stage
		 * 数据手册 22.7: 先写 UADD, 状态阶段之后再置 ADDEN
		 */
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
		if (lo(setup->value) > USB_CONF_VALUE) {
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
 *
 * 看门狗在状态阶段发出之后触发, 主机看到设备离开总线. 在此之前任务照常运行.
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
	/*
	 * datasheet 22.12.2: a SETUP overrides whatever was in progress
	 * 数据手册 22.12.2: SETUP 取代正在进行的任何操作
	 */
	UEINTX = ~((1 << RXSTPI) | (1 << RXOUTI) | (1 << TXINI));

	if ((setup.request_type & REQ_TYPE_MASK) == REQ_TYPE_STANDARD)
		standard_request(&setup);
	else if ((setup.request_type & REQ_TYPE_MASK) == REQ_TYPE_CLASS)
		class_request(&setup);
	else
		ep0_stall();
}

/*
 * Move what the host wrote into acm_rx_buf, datasheet 22.13.2.  A bank
 * that does not fit stays in the controller with its interrupt masked;
 * acm_read() asks for another pass once it has made room.
 *
 * 把主机写入的数据移入 acm_rx_buf (数据手册 22.13.2). 放不下的数据块留在控制器
 * 中, 其中断被屏蔽; acm_read() 腾出空间之后再请求处理一次.
 */
static void cdc_rx(void)
{
	UENUM = CDC_RX_EP;
	while (UEINTX & (1 << FIFOCON)) {
		UEINTX = ~(1 << RXOUTI);
		while (UEINTX & (1 << RWAL)) {
			if ((uint8_t)(acm_rx_head - acm_rx_tail) == ACM_RX_SIZE) {
				acm_rx_held = 1;
				goto out;
			}
			acm_rx_buf[acm_rx_head % ACM_RX_SIZE] = UEDATX;
			acm_rx_head++;
		}
		UEINTX = (uint8_t)~(1 << FIFOCON);
	}
	acm_rx_held = 0;
	UEIENX = 1 << RXOUTE;
out:
	wake_up(&acm_rx_wq);
}

/*
 * Send @len bytes as one packet on the data IN endpoint.  Returns -1 if
 * the bank stays full, which happens when nothing on the host reads the
 * port.  @len is at most CDC_DATA_SIZE.
 *
 * 把 @len 字节作为一个包从数据 IN 端点发出. 数据块一直为满时返回 -1, 主机上没有
 * 程序读取串口时即如此. @len 至多为 CDC_DATA_SIZE.
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
 *
 * 发送 console_sent 之后的记录. 打开串口时从日志中仍保留的最旧记录开始, 并报告
 * 其前有多少条记录. 来不及发送就被覆盖的记录以同样方式计数. 主机停止读取时, 余
 * 下的记录被丢弃, 不再重试, 使停滞的串口每条记录只耗费一次超时.
 *
 * 标为 noinline, 使其行缓冲区与记录不留在 usb_task() 的栈帧中, 位于
 * standard_request() 的 printk() 之下.
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

/*
 * taken in wait_event(), with interrupts off
 * 在 wait_event() 中关中断取出
 */
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

/*
 * Send what acm_write() has queued, after any log records.  While the
 * port is closed the bytes stay queued, so that the shell's prompt from
 * boot appears once a terminal opens the port; a writer that fills the
 * queue meanwhile waits for the next open.  If the host holds the port
 * open but stops reading, the queue is dropped, so that a writer does
 * not wait on a terminal that is gone.
 *
 * 在日志记录之后发送 acm_write() 排队的数据. 串口关闭期间数据保留在队列中, 使开
 * 机时 shell 的提示符在终端打开串口后出现; 其间填满队列的写入者等待下一次打开.
 * 主机保持串口打开却停止读取时丢弃队列, 使写入者不会一直等待已消失的终端.
 */
static void acm_flush(void)
{
	uint8_t n;

	if (!console_open())
		return;
	UENUM = CDC_TX_EP;
	while (acm_tx_head != acm_tx_tail) {
		if (!ep_wait(1 << TXINI)) {
			acm_tx_tail = acm_tx_head;
			break;
		}
		UEINTX = ~(1 << TXINI);
		for (n = 0; n < ACM_TX_PACKET && acm_tx_head != acm_tx_tail; n++) {
			UEDATX = acm_tx_buf[acm_tx_tail % ACM_TX_SIZE];
			acm_tx_tail++;
		}
		UEINTX = (uint8_t)~(1 << FIFOCON);
		wake_up(&acm_tx_wq);
	}
	UENUM = 0;
	wake_up(&acm_tx_wq);
}

static void usb_kick(uint8_t events)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		usb_events |= events;
	}
	wake_up(&usb_wq);
}

static int acm_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t n = 0;

	(void)f;
	wait_event(&acm_rx_wq, acm_rx_head != acm_rx_tail);
	while (n < count && acm_rx_head != acm_rx_tail) {
		buf[n++] = acm_rx_buf[acm_rx_tail % ACM_RX_SIZE];
		acm_rx_tail++;
	}
	if (acm_rx_held)
		usb_kick(USB_EV_RX);
	return n;
}

static int acm_write(struct file *f, const char *buf, uint8_t count)
{
	uint8_t n;

	(void)f;
	for (n = 0; n < count; n++) {
		wait_event(&acm_tx_wq,
			   (uint8_t)(acm_tx_head - acm_tx_tail) != ACM_TX_SIZE);
		acm_tx_buf[acm_tx_head % ACM_TX_SIZE] = buf[n];
		acm_tx_head++;
	}
	usb_kick(0);
	return count;
}

static const struct file_operations acm_fops = {
	.read = acm_read,
	.write = acm_write,
};

/*
 * the console kick from printk(), possibly in an interrupt
 * printk() 发来的控制台通知, 可能在中断中调用
 */
static void usb_console_kick(void)
{
	wake_up(&usb_wq);
}

/*
 * Serves what the endpoint interrupt reported, then unmasks it.  A
 * SETUP is looked for whenever the task wakes, since a bus reset in the
 * middle of a request leaves endpoint 0 unmasked again.
 *
 * 处理端点中断报告的事件, 然后解除屏蔽. 任务每次醒来都检查 SETUP, 因为请求进行
 * 中的总线复位会使端点 0 重新处于未屏蔽状态.
 */
static void usb_task(void)
{
	uint8_t events;

	for (;;) {
		wait_event(&usb_wq,
			   (events = usb_take_events()) || console_pending() ||
			   (console_open() && acm_tx_head != acm_tx_tail));
		if (events & USB_EV_SETUP) {
			ep0_setup();
			UENUM = 0;
			UEIENX = 1 << RXSTPE;
		}
		if (events & USB_EV_RX)
			cdc_rx();
		UENUM = 0;
		if (console_open())
			console_flush();
		acm_flush();
	}
}

/**
 * usb_general_interrupt() - handle the end of a bus reset
 *
 * Every endpoint but 0 is gone after a bus reset, datasheet 22.4.
 *
 * 总线复位结束: 除端点 0 外的所有端点都已消失 (数据手册 22.4).
 *
 * Context: the USB general vector in switch.S, interrupts disabled.
 * 由 switch.S 中的 USB general 向量在关中断状态下调用.
 * Return: 0; no task is woken.
 * 返回 0, 不唤醒任何任务.
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

/**
 * usb_endpoint_interrupt() - hand an endpoint event to usb_task()
 *
 * Masks the endpoint that raised the interrupt.  usb_task() may be in
 * the middle of using UENUM, so the selection is put back.
 *
 * 屏蔽引发中断的端点, 交给 usb_task() 处理. usb_task() 可能正在使用 UENUM, 因此
 * 恢复原来的选择.
 *
 * Context: the USB endpoint vector in switch.S, interrupts disabled.
 * 由 switch.S 中的 USB endpoint 向量在关中断状态下调用.
 * Return: the number of tasks woken, for switch.S.
 * 返回被唤醒的任务数, 供 switch.S 使用.
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

/**
 * usb_init() - power on the USB controller and start usb_task()
 *
 * Follows datasheet 21.12, "Power On the USB interface".  The board then
 * attaches on its own whenever VBUS is present, datasheet 22.2.
 *
 * 按数据手册 21.12 "Power On the USB interface" 为 USB 控制器上电, 并启动
 * usb_task(). 此后只要 VBUS 存在, 板子就自行接入总线 (数据手册 22.2).
 *
 * Context: before sched_start(), interrupts disabled.
 * 在 sched_start() 之前、关中断状态下调用.
 * Return: 0, or -1 with the controller left off if the PLL does not
 * lock or the task cannot be created.
 * 成功时返回 0; PLL 未锁定或无法创建任务时返回 -1, 控制器保持关闭.
 */
int usb_init(void)
{
	uint16_t spins = USB_PLL_TIMEOUT_MS * 1000UL / USB_SPIN_US;

	if (task_create(usb_task, usb_task_stack, sizeof(usb_task_stack)) < 0)
		return -1;
	register_console(usb_console_kick);
	tty_register(PSTR("ttyACM0"), &acm_fops);

	UHWCON = 1 << UVREGE;
	USBCON = (1 << USBE) | (1 << FRZCLK);
	/*
	 * 48 MHz, straight to USB, from the 16 MHz crystal halved
	 * PLL 输出 48 MHz 直接供给 USB, 输入为 16 MHz 晶振的二分频
	 */
	PLLFRQ = 1 << PDIV2;
	PLLCSR = (1 << PINDIV) | (1 << PLLE);
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
	/* full speed, attach; 全速, 接入 */
	UDCON = 0;
	UDIEN = 1 << EORSTE;
	return 0;
}
