// SPDX-License-Identifier: GPL-2.0-only
/*
 * HD44780 character LCD behind a PCF8574 I2C expander, 4-bit mode,
 * write only.
 *
 * The backpack board decides how the expander pins are wired, and boards
 * differ.  The mapping below is the common one: P0 RS, P1 RW, P2 E,
 * P3 backlight, P4..P7 to DB4..DB7.
 *
 * RW is held low, so the busy flag is never read.  Each nibble is one I2C
 * transaction of an address and three data bytes, about 0.4 ms at
 * 100 kHz, longer than the 37 us most instructions need (HD44780U
 * table 6).  Only clear needs an explicit wait.
 *
 * 经 PCF8574 I2C 扩展器连接的 HD44780 字符 LCD, 4 位模式, 只写.
 *
 * 扩展器各引脚的接法由转接板决定, 各家转接板并不相同. 下面采用常见的接法:
 * P0 RS, P1 RW, P2 E, P3 背光, P4..P7 接 DB4..DB7.
 *
 * RW 保持低电平, 因此从不读取忙标志. 每个半字节是一次 I2C 传输, 含一个地址
 * 与三个数据字节, 在 100 kHz 下约 0.4 ms, 长于大多数指令所需的 37 us
 * (HD44780U 表 6). 只有清屏需要显式等待.
 */
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <leonix/i2c.h>
#include <leonix/lcd.h>
#include <leonix/panic.h>
#include <leonix/sched.h>

#define PCF_RS		(1 << 0)
#define PCF_RW		(1 << 1)
#define PCF_E		(1 << 2)
#define PCF_BACKLIGHT	(1 << 3)

/*
 * PCF8574 at 0x20..0x27, PCF8574A at 0x38..0x3f
 * PCF8574 的地址为 0x20..0x27, PCF8574A 为 0x38..0x3f
 */
#define PCF8574_BASE	0x20
#define PCF8574A_BASE	0x38
#define PCF_NR_ADDRS	8

#define LCD_CLEAR		0x01
#define LCD_ENTRY_INC		0x06	/* cursor moves right, no shift */
#define LCD_DISPLAY_OFF		0x08
#define LCD_DISPLAY_ON		0x0c	/* no cursor, no blink */
#define LCD_FUNC_4BIT_2LINE	0x28	/* 4-bit, 2 lines, 5x8 dots */
#define LCD_SET_DDRAM		0x80

/*
 * Table 6 gives no execution time for clear display.  Return home, the
 * other instruction that resets the address counter, lists 1.52 ms at
 * 270 kHz; 2 ms leaves a margin for a slower oscillator.
 *
 * 表 6 没有给出清屏的执行时间. 另一条复位地址计数器的指令 Return home 在
 * 270 kHz 下为 1.52 ms; 取 2 ms 为较慢的振荡器留出余量.
 */
#define LCD_CLEAR_MS	2

static uint8_t pcf_addr;

/*
 * After panic() the scheduler must not be entered, so the wait becomes a
 * busy loop.  The HD44780 waits are lower bounds, which either meets.
 *
 * panic() 之后不得再进入调度器, 因此等待改为忙等. HD44780 的等待时间都只有
 * 下限, 两种方式都能满足.
 */
static void lcd_delay(uint16_t ms)
{
	if (!oops_in_progress) {
		msleep(ms);
		return;
	}
	while (ms--)
		_delay_ms(1);
}

/*
 * E must rise after RS and the data are stable, and the HD44780 latches
 * on the falling edge, hence three bytes: set up, E high, E low.
 *
 * E 必须在 RS 与数据稳定之后升高, HD44780 在下降沿锁存, 因此发送三个字节:
 * 建立数据、E 置高、E 置低.
 */
static int lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
	uint8_t out = (uint8_t)(nibble << 4) | rs | PCF_BACKLIGHT;
	uint8_t seq[3] = { out, out | PCF_E, out };

	return i2c_write(pcf_addr, seq, sizeof(seq));
}

static int lcd_write_byte(uint8_t byte, uint8_t rs)
{
	if (lcd_write_nibble(byte >> 4, rs) < 0)
		return -1;
	return lcd_write_nibble(byte & 0x0f, rs);
}

static int lcd_command(uint8_t cmd)
{
	return lcd_write_byte(cmd, 0);
}

static int pcf_probe(void)
{
	static const uint8_t bases[] = { PCF8574_BASE, PCF8574A_BASE };
	uint8_t b, i;

	for (b = 0; b < sizeof(bases); b++)
		for (i = 0; i < PCF_NR_ADDRS; i++)
			if (i2c_write(bases[b] + i, 0, 0) == 0) {
				pcf_addr = bases[b] + i;
				return 0;
			}
	return -1;
}

static int lcd_clear(void)
{
	if (lcd_command(LCD_CLEAR) < 0)
		return -1;
	lcd_delay(LCD_CLEAR_MS);
	return 0;
}

/**
 * lcd_init() - find the expander and initialise the display
 *
 * Follows HD44780U figure 24.  An earlier run may have left the display
 * in 4-bit mode with half a byte latched; the three 0x3 nibbles
 * resynchronise it from there as well as after power on.
 *
 * 查找扩展器并初始化显示屏. 按 HD44780U 图 24 进行. 先前的运行可能使显示屏
 * 停在 4 位模式且已锁存半个字节; 三个 0x3 半字节在这种状态下与上电之后
 * 一样能使显示屏重新同步.
 *
 * Context: a task, or panic().  任务中或 panic() 中调用.
 * Return: 0, or -1 if no expander answers or a write fails.
 * 成功时返回 0, 没有扩展器应答或写入失败时返回 -1.
 */
int lcd_init(void)
{
	i2c_init();
	if (pcf_probe() < 0)
		return -1;

	lcd_delay(50);			/* > 40 ms after VCC rises to 2.7 V */
	if (lcd_write_nibble(0x3, 0) < 0)
		return -1;
	lcd_delay(5);			/* > 4.1 ms */
	if (lcd_write_nibble(0x3, 0) < 0)
		return -1;
	lcd_delay(1);			/* > 100 us */
	if (lcd_write_nibble(0x3, 0) < 0 ||
	    lcd_write_nibble(0x2, 0) < 0)
		return -1;

	if (lcd_command(LCD_FUNC_4BIT_2LINE) < 0 ||
	    lcd_command(LCD_DISPLAY_OFF) < 0 ||
	    lcd_clear() < 0 ||
	    lcd_command(LCD_ENTRY_INC) < 0 ||
	    lcd_command(LCD_DISPLAY_ON) < 0)
		return -1;
	return 0;
}

/**
 * lcd_set_cursor() - move the cursor
 * @col: column, clamped to LCD_COLS - 1
 * @row: row, clamped to LCD_ROWS - 1
 *
 * The second line starts at DDRAM 0x40 (HD44780U, Set DDRAM Address).
 *
 * 把光标移到第 @row 行第 @col 列, 超出范围时取最大值. 第二行从 DDRAM 0x40
 * 开始 (HD44780U, Set DDRAM Address).
 *
 * Return: 0, or -1 if the write fails.  成功时返回 0, 写入失败时返回 -1.
 */
int lcd_set_cursor(uint8_t col, uint8_t row)
{
	static const uint8_t row_start[LCD_ROWS] = { 0x00, 0x40 };

	if (col >= LCD_COLS)
		col = LCD_COLS - 1;
	if (row >= LCD_ROWS)
		row = LCD_ROWS - 1;
	return lcd_command(LCD_SET_DDRAM | (row_start[row] + col));
}

/**
 * lcd_puts() - write a string at the cursor
 * @s: the string
 *
 * 在光标处写入字符串 @s.
 *
 * Return: 0, or -1 if a write fails.  成功时返回 0, 写入失败时返回 -1.
 */
int lcd_puts(const char *s)
{
	while (*s)
		if (lcd_write_byte((uint8_t)*s++, PCF_RS) < 0)
			return -1;
	return 0;
}

/**
 * lcd_check() - check that the display kept the state lcd_init() gave it
 *
 * A backpack unplugged and plugged back between two transfers misses no
 * ACK, yet its HD44780 is back in 8-bit mode.  Its PCF8574 has reset too,
 * and the outputs read back high (TI SCPS068K, power-on reset), while
 * every byte the driver writes last leaves E and RW low.
 *
 * Only E and RW are compared: they drive HD44780 inputs and read back as
 * written.  The backlight pin reads low while written high on the board
 * I tested, and comparing the whole byte reported a replug on every
 * refresh.
 *
 * 检查显示屏是否保持 lcd_init() 设定的状态. 转接板在两次传输之间被拔下再
 * 插回时不会漏掉 ACK, 但其 HD44780 已回到 8 位模式. PCF8574 同样已复位,
 * 输出读回为高电平 (TI SCPS068K, 上电复位), 而驱动最后写入的每个字节都使
 * E 与 RW 为低电平.
 *
 * 只比较 E 与 RW: 这两个引脚驱动 HD44780 的输入, 读回值与写入值一致. 在我
 * 测试的板上, 背光引脚写高时读回为低, 比较整个字节会在每次刷新时都报告重新
 * 插入.
 *
 * Return: 1 if the display is as left, 0 if it must be initialised
 * again, or -1 if the expander does not answer.  显示屏保持原状时返回 1,
 * 需要重新初始化时返回 0, 扩展器无应答时返回 -1.
 */
int lcd_check(void)
{
	uint8_t port;

	if (i2c_read(pcf_addr, &port, 1) < 0)
		return -1;
	return !(port & (PCF_E | PCF_RW));
}

/**
 * lcd_address() - the expander's I2C address
 *
 * Return: the address lcd_init() found, or 0 before that.
 * lcd_init() 找到的扩展器 I2C 地址; 此前为 0.
 */
uint8_t lcd_address(void)
{
	return pcf_addr;
}
