// SPDX-License-Identifier: GPL-2.0-only
/*
 * USART1 as the serial terminal ttyS0 and the kernel log's console, at
 * 115200 baud on PA9 (TX) and PA10 (RX), their default alternate
 * functions (DS5319 table 5).
 *
 * Both directions go through rings that the interrupt empties and
 * fills; a reader sleeps until a byte arrives, a writer until there is
 * room.  Log records go into the transmit ring whole, as the AVR's USB
 * console sends them a line at a time: printk() tells usart_console_kick(),
 * and the interrupt adds more once the ring has room for a line again.
 * A record that the log overwrites before it fits is counted as lost,
 * and one of a level that suppress_message_printing() leaves out is
 * skipped.
 *
 * The interrupt has the priority of SysTick and PendSV, see switch.S,
 * and pends PendSV when it has woken a task, so that the reader runs
 * before the next tick.
 *
 * Register sequences follow RM0008 Rev 15 chapter 27.
 *
 * USART1 作为串口终端 ttyS0 与内核日志的控制台, 以 115200 波特率工作在 PA9 (TX)
 * 与 PA10 (RX) 上, 即这两个引脚的默认复用功能 (DS5319 表 5).
 *
 * 两个方向都经过由中断清空与填充的环形缓冲区; 读者睡眠到有字节到达, 写者睡眠
 * 到有空间. 日志记录整条放入发送缓冲区, 与 AVR 的 USB 控制台逐行发送相同:
 * printk() 通知 usart_console_kick(), 缓冲区重新放得下一行时由中断继续加入.
 * 放入之前就被日志覆盖的记录计为丢失, suppress_message_printing() 略去的
 * 级别的记录被跳过.
 *
 * 该中断与 SysTick、PendSV 同一优先级 (见 switch.S), 唤醒了任务时挂起 PendSV,
 * 使读者在下一个节拍之前运行.
 *
 * 寄存器的操作顺序依照 RM0008 Rev 15 第 27 章.
 */
#include <stdint.h>

#include <asm/setup.h>
#include <asm/stm32f1.h>
#include <asm/usart.h>

#include <leonix/irqflags.h>
#include <leonix/kernel.h>
#include <leonix/module.h>
#include <leonix/pgmspace.h>
#include <leonix/printk.h>
#include <leonix/tty.h>
#include <leonix/wait.h>

#define USART_BAUD	115200u
#define TX_PIN		9
#define RX_PIN		10
#define TX_CRH_SHIFT	((TX_PIN - 8) * 4)
#define RX_CRH_SHIFT	((RX_PIN - 8) * 4)

/*
 * Sizes divide 256, so the uint8_t indices wrap with them.  A writer is
 * woken once half the transmit ring is free, not after every byte.
 *
 * 大小整除 256, 因此 uint8_t 下标与之同步回绕. 发送缓冲区空出一半时才唤醒写者,
 * 不在每个字节之后唤醒.
 */
#define RX_SIZE		32
#define TX_SIZE		128
#define TX_WAKE_ROOM	(TX_SIZE / 2)

/*
 * "[secs] text\r\n" with a 5-digit stamp, the terminator included
 * 带 5 位时间戳的 "[秒] 文本\r\n", 含结束符
 */
#define CONSOLE_LINE	(sizeof("[65535] \r\n") + LOG_TEXT - 1)

_Static_assert(256 % RX_SIZE == 0 && 256 % TX_SIZE == 0,
	       "ring sizes must divide the range of the uint8_t indices");
_Static_assert(CONSOLE_LINE <= TX_WAKE_ROOM,
	       "a log line must fit in the room a writer is woken for");

static char rx_buf[RX_SIZE];
static volatile uint8_t rx_head, rx_tail;
static struct wait_queue rx_wq = WAIT_QUEUE_INIT;

static char tx_buf[TX_SIZE];
static volatile uint8_t tx_head, tx_tail;
static struct wait_queue tx_wq = WAIT_QUEUE_INIT;

/*
 * the newest record in tx_buf, and the records lost since
 * 已放入 tx_buf 的最新记录, 以及其后丢失的记录数
 */
static uint16_t console_sent;
static uint16_t console_lost;

static uint8_t tx_room(void)
{
	return TX_SIZE - (uint8_t)(tx_head - tx_tail);
}

/*
 * Append @len bytes, which must fit, and let the interrupt send them.
 * Interrupts disabled.
 *
 * 追加 @len 字节 (必须放得下), 由中断发送. 在关中断状态下调用.
 */
static void tx_put(const char *s, uint8_t len)
{
	while (len--) {
		tx_buf[tx_head % TX_SIZE] = *s++;
		tx_head++;
	}
	USART1_CR1 |= USART_CR1_TXEIE;
}

/*
 * Move as many records after console_sent into tx_buf as fit whole.
 * Interrupts disabled, so no printk() can add a record meanwhile.
 *
 * 把 console_sent 之后的记录放入 tx_buf, 放得下几条就放几条, 每条完整放入. 在关
 * 中断状态下调用, 其间不会有 printk() 加入记录.
 */
static void console_fill(void)
{
	struct log_record rec;
	char line[CONSOLE_LINE];
	uint16_t newest = log_newest();
	uint8_t len;

	while (console_sent != newest && tx_room() >= CONSOLE_LINE) {
		if (log_read(console_sent + 1, &rec) < 0) {
			console_sent++;
			console_lost++;
			continue;
		}
		if (console_lost) {
			len = snprintk(line, sizeof(line), "(%u lost)\r\n",
				       console_lost);
			console_lost = 0;
		} else if (suppress_message_printing(rec.level)) {
			console_sent++;
			continue;
		} else {
			len = snprintk(line, sizeof(line), "[%5u] %s\r\n",
				       rec.secs, rec.text);
			console_sent++;
		}
		tx_put(line, len);
	}
}

/*
 * the console kick from printk(), possibly in an interrupt
 * printk() 发来的控制台通知, 可能在中断中调用
 */
static void usart_console_kick(void)
{
	unsigned long flags;

	local_irq_save(flags);
	console_fill();
	local_irq_restore(flags);
}

/*
 * Reading SR and then DR clears RXNE and ORE (RM0008 27.6.1); a byte
 * that finds rx_buf full is dropped.  The transmit side is served only
 * while TXEIE is set, since TXE stays set while the ring is empty.
 *
 * 先读 SR 再读 DR 即清除 RXNE 与 ORE (RM0008 27.6.1); rx_buf 已满时丢弃该字节.
 * 发送方向只在 TXEIE 置位时处理, 因为缓冲区为空时 TXE 一直置位.
 */
static void usart1_interrupt(void)
{
	uint32_t sr = USART1_SR;
	uint8_t woken = 0;
	char c;

	if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
		c = USART1_DR;
		if ((uint8_t)(rx_head - rx_tail) != RX_SIZE) {
			rx_buf[rx_head % RX_SIZE] = c;
			rx_head++;
		}
		woken += wake_up(&rx_wq);
	}
	if ((USART1_CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
		if (tx_head != tx_tail) {
			USART1_DR = tx_buf[tx_tail % TX_SIZE];
			tx_tail++;
		}
		console_fill();
		if (tx_head == tx_tail)
			USART1_CR1 &= ~USART_CR1_TXEIE;
		if (tx_room() >= TX_WAKE_ROOM)
			woken += wake_up(&tx_wq);
	}
	if (woken)
		SCB_ICSR = SCB_ICSR_PENDSVSET;
}

/*
 * vector 16 + USART1_IRQ, see start.S
 * 向量 16 + USART1_IRQ, 见 start.S
 */
void irq37_handler(void) __attribute__((alias("usart1_interrupt")));

_Static_assert(USART1_IRQ == 37, "irq37_handler is named for USART1_IRQ");

static int usart_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t n = 0;

	(void)f;
	wait_event(&rx_wq, rx_head != rx_tail);
	while (n < count && rx_head != rx_tail) {
		buf[n++] = rx_buf[rx_tail % RX_SIZE];
		rx_tail++;
	}
	return n;
}

static int usart_write(struct file *f, const char *buf, uint8_t count)
{
	unsigned long flags;
	uint8_t n = 0;

	(void)f;
	while (n < count) {
		wait_event(&tx_wq, tx_room() != 0);
		local_irq_save(flags);
		while (n < count && tx_room()) {
			tx_buf[tx_head % TX_SIZE] = buf[n++];
			tx_head++;
		}
		USART1_CR1 |= USART_CR1_TXEIE;
		local_irq_restore(flags);
	}
	return count;
}

static const struct file_operations usart_fops = {
	.read = usart_read,
	.write = usart_write,
};

/**
 * usart_panic_write() - send bytes with the interrupt off
 * @s: the bytes
 * @len: how many
 *
 * Sends what tx_buf and the log still hold first, so that the last
 * records come out before panic()'s own line.
 *
 * 在不使用中断的情况下发送 @s 中的 @len 字节. 先发出 tx_buf 与日志中尚未发送的
 * 内容, 使最后几条记录出现在 panic() 自己的那一行之前.
 *
 * Context: panic(), interrupts disabled.
 * 由 panic() 在关中断状态下调用.
 */
void usart_panic_write(const char *s, uint8_t len)
{
	for (;;) {
		console_fill();
		if (tx_head == tx_tail)
			break;
		while (tx_head != tx_tail) {
			while (!(USART1_SR & USART_SR_TXE))
				;
			USART1_DR = tx_buf[tx_tail % TX_SIZE];
			tx_tail++;
		}
	}
	while (len--) {
		while (!(USART1_SR & USART_SR_TXE))
			;
		USART1_DR = *s++;
	}
}

/*
 * The transmitter is enabled before PA9 becomes its output: setting TE
 * sends an idle frame (RM0008 27.3.2), so the line goes straight to
 * high.  On the author's board an alternate-function TX pin is not
 * driven while its USART is off, see Documentation/stm32f103.rst.  PA10
 * gets a pull-up, so that a terminal left unconnected reads as idle.
 *
 * 发送器先于 PA9 成为其输出而使能: 置位 TE 时发送一个空闲帧 (RM0008
 * 27.3.2), 线路因此直接变为高电平. 在作者的板子上, USART 关闭时复用功能的 TX
 * 引脚不被驱动, 见 Documentation/stm32f103.rst. PA10 加上拉, 使未连接终端时读为
 * 空闲.
 */
static int usart_init(void)
{
	RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
	USART1_BRR = (cpu_hz + USART_BAUD / 2) / USART_BAUD;
	USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
		     USART_CR1_RXNEIE;

	GPIO_BSRR(GPIOA_BASE) = 1u << RX_PIN;
	GPIO_CRH(GPIOA_BASE) = (GPIO_CRH(GPIOA_BASE) &
				~(GPIO_MODE_MASK << TX_CRH_SHIFT |
				  GPIO_MODE_MASK << RX_CRH_SHIFT)) |
			       GPIO_AF_PP_2MHZ << TX_CRH_SHIFT |
			       GPIO_INPUT_PULL << RX_CRH_SHIFT;

	*(volatile uint8_t *)(NVIC_IPR_BASE + USART1_IRQ) = IRQ_PRIO_KERNEL;
	NVIC_ISER(USART1_IRQ / 32) = 1u << (USART1_IRQ % 32);

	register_console(usart_console_kick);
	usart_console_kick();
	return tty_register(PSTR("ttyS0"), &usart_fops);
}
module_init(usart_init);
