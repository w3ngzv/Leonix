// SPDX-License-Identifier: GPL-2.0-only
/*
 * The TTP229 capacitive touch keypad, in 16-key mode or with
 * CONFIG_TTP229_8_KEYS in 8-key mode, as /dev/keypad, so
 * that programs can take input with no computer on the USB port.  Each
 * read() sleeps until a key goes down and returns one character per key
 * that went down, from keypad_chars[].  The HW-136 board prints 1 to 16
 * on the pads, TP0 to TP15 in the datasheet.
 *
 * A0 (PF7) drives SCL, A1 (PF6) reads SDO; nothing else uses port F, and
 * PF4..PF7 are port pins because JTAGEN is unprogrammed (booting.rst).
 * The module runs from the board's 5 V, so SDO swings the full 5 V.
 *
 * The chip powers up in 8-key mode.  16-key mode needs TP2 (KYSEL) tied
 * to VSS through a high-value resistor, TTP229 datasheet section 2,
 * which on the HW-136 is the jumper at P1 pin 3; until it is closed,
 * CONFIG_TTP229_8_KEYS reads keys 1 to 8.  With TP1 (SAHL) left
 * open SCL idles high and SDO is active low: a touched key reads 0
 * (section 3-4, figure 3-4-2).  The internal pull-up on SDO makes a
 * missing module read as no key down.
 *
 * Only a reader polls, once every KEYPAD_POLL_MS, so a key touched and
 * released while nobody reads is never seen.
 *
 * TTP229 电容触摸键盘, 工作在 16 键模式, 或在 CONFIG_TTP229_8_KEYS 下工作在
 * 8 键模式, 登记为 /dev/keypad, 使程序在 USB 口没有
 * 连接电脑时也能取得输入. 每次 read() 睡眠到有键按下, 每个新按下的键返回
 * keypad_chars[] 中的一个字符. HW-136 板在触摸区上印的是 1 到 16, 即数据手册
 * 中的 TP0 到 TP15.
 *
 * A0 (PF7) 驱动 SCL, A1 (PF6) 读取 SDO; 端口 F 没有其他用途, JTAGEN 未编程,
 * PF4..PF7 是普通端口引脚 (booting.rst). 模块由板上 5 V 供电, SDO 的摆幅因此
 * 是完整的 5 V.
 *
 * 芯片上电后处于 8 键模式. 16 键模式要求 TP2 (KYSEL) 经高阻值电阻接 VSS,
 * 见 TTP229 数据手册第 2 节, 在 HW-136 上即 P1 第 3 脚的跳线; 该跳线连通之前,
 * 以 CONFIG_TTP229_8_KEYS 读取键 1 到 8. TP1 (SAHL) 悬空
 * 时 SCL 空闲为高, SDO 低电平有效: 被触摸的键读为 0 (第 3-4 节, 图 3-4-2).
 * SDO 打开内部上拉, 没有接模块时读到的是没有键按下.
 *
 * 只有读者在轮询, 每 KEYPAD_POLL_MS 一次, 无人读取时按下又松开的键不会被看到.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <leonix/errno.h>
#include <leonix/exec.h>
#include <leonix/fs.h>
#include <leonix/module.h>
#include <leonix/sched.h>

#define KEYPAD_SCL	PF7
#define KEYPAD_SDO	PF6
/*
 * In 8-key mode the frame is D0..D7 (figure 3-4-4); what SDO does on
 * further clocks is not specified.
 *
 * 8 键模式下一帧为 D0..D7 (图 3-4-4), 其后再给时钟时 SDO 的状态没有规定.
 */
#define KEYPAD_KEYS	(IS_ENABLED(CONFIG_TTP229_8_KEYS) ? 8 : 16)

/*
 * 25 kHz, inside the 1 kHz to 512 kHz F_SCL of the datasheet table under
 * figure 3-4-4
 *
 * 25 kHz, 在数据手册图 3-4-4 下表中 F_SCL 的 1 kHz 至 512 kHz 之内
 */
#define HALF_CYCLE_US	20

/*
 * SCL left alone for Tout, 2 ms, resets the interface, so every poll
 * starts again at D0 (section 3-4).  T_resp for 16 keys is 32 ms.
 *
 * SCL 保持 Tout 即 2 ms 不变时接口复位, 因此每次轮询都从 D0 开始 (第 3-4 节).
 * 16 键模式的 T_resp 为 32 ms.
 */
#define KEYPAD_TOUT_MS	2
#define KEYPAD_POLL_MS	20
_Static_assert(KEYPAD_POLL_MS > KEYPAD_TOUT_MS,
	       "a poll would continue the previous frame instead of D0");

/* key 1 is TP0; 键 1 即 TP0 */
static const char keypad_chars[] PROGMEM = "123456789abcdefg";
_Static_assert(sizeof(keypad_chars) >= KEYPAD_KEYS + 1,
	       "a key would have no character");

/* bit n set while key n + 1 is down; 键 n + 1 按下时第 n 位为 1 */
static uint16_t keypad_down;

/*
 * The chip changes SDO after the falling edge of SCL (figure 3-4-2), so
 * a bit is read after the rising edge that follows.  An interrupt may
 * stretch a cycle; only a stretch past Tout would lose the frame.
 *
 * 芯片在 SCL 下降沿之后改变 SDO (图 3-4-2), 因此在随后的上升沿之后读取一位.
 * 中断可能拉长某个周期, 只有超过 Tout 的拉长才会使这一帧失效.
 */
static uint16_t keypad_scan(void)
{
	uint16_t down = 0;
	uint8_t i;

	for (i = 0; i < KEYPAD_KEYS; i++) {
		PORTF &= ~(1 << KEYPAD_SCL);
		_delay_us(HALF_CYCLE_US);
		PORTF |= 1 << KEYPAD_SCL;
		_delay_us(HALF_CYCLE_US);
		if (!(PINF & (1 << KEYPAD_SDO)))
			down |= 1U << i;
	}
	return down;
}

static int keypad_read(struct file *f, char *buf, uint8_t count)
{
	uint16_t now, pressed;
	uint8_t n = 0, i;

	(void)f;
	for (;;) {
		now = keypad_scan();
		pressed = now & ~keypad_down;
		keypad_down = now;
		if (pressed)
			break;
		if (exec_interrupted())
			return -EINTR;
		msleep(KEYPAD_POLL_MS);
	}
	for (i = 0; i < KEYPAD_KEYS && n < count; i++)
		if (pressed & (1U << i))
			buf[n++] = pgm_read_byte(&keypad_chars[i]);
	return n;
}

static const struct file_operations keypad_fops = {
	.read = keypad_read,
};

static int keypad_init(void)
{
	PORTF |= 1 << KEYPAD_SCL;
	DDRF |= 1 << KEYPAD_SCL;
	PORTF |= 1 << KEYPAD_SDO;
	return register_chrdev(PSTR("keypad"), &keypad_fops);
}
module_init(keypad_init);
