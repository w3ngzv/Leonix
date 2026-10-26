// SPDX-License-Identifier: GPL-2.0-only
/*
 * TWI master.
 *
 * i2c_write() and i2c_read() hand a transfer to the TWI interrupt and
 * sleep until the interrupt reports it done, so the CPU is free while
 * the bytes go out.  A mutex serialises callers.  Only a task may call
 * them, after sched_start().
 *
 * panic() runs with interrupts off, so there writes are polled instead,
 * see i2c_write_polled().
 *
 * SDA is PD1 and SCL is PD0, D2 and D3 on the Leonardo header.
 *
 * The bus can be unplugged and plugged back while running.  A failed
 * transfer ends in i2c_reset(), which also frees a slave left holding
 * SDA low, so the next i2c_write() starts from an idle bus.
 *
 * TWI 主机.
 *
 * i2c_write() 与 i2c_read() 把传输交给 TWI 中断, 然后睡眠到中断报告完成, 字节发
 * 送期间 CPU 因此空闲. 一个互斥锁使调用者依次进行. 只允许任务在 sched_start()
 * 之后调用.
 *
 * panic() 在关中断的状态下运行, 因此那里的写入改为轮询, 见 i2c_write_polled().
 *
 * SDA 为 PD1, SCL 为 PD0, 即 Leonardo 排针上的 D2 与 D3.
 *
 * 运行期间可以拔下并重新插上总线. 失败的传输以 i2c_reset() 结束, i2c_reset() 同
 * 时释放把 SDA 拉低不放的从机, 因此下一次 i2c_write() 从空闲的总线开始.
 */
#include <avr/io.h>
#include <util/delay.h>
#include <util/twi.h>

#include <leonix/i2c.h>
#include <leonix/jiffies.h>
#include <leonix/mutex.h>
#include <leonix/panic.h>

/*
 * PCF8574 is a standard-mode part, 100 kHz at most
 * PCF8574 属标准模式器件, 最高 100 kHz
 */
#define I2C_SCL_HZ	100000UL
#define I2C_TWBR	((F_CPU / I2C_SCL_HZ - 16) / 2)

/*
 * one byte takes 90 us at 100 kHz; near 2 ms means a stuck bus
 * 100 kHz 下一个字节需要 90 us; 接近 2 ms 即说明总线卡死
 */
#define I2C_TIMEOUT_MS	2

/*
 * polling step of a timeout after panic(), see i2c_timed_out()
 * panic() 之后超时等待的轮询步长, 见 i2c_timed_out()
 */
#define I2C_SPIN_US	10

/*
 * UM10204 3.1.16: a slave stuck holding SDA low lets go within nine
 * clocks.  Each half clock is 5 us, above the 4.7 us tLOW and 4.0 us
 * tHIGH of standard mode (UM10204 table 11).
 *
 * UM10204 3.1.16: 把 SDA 拉低不放的从机在九个时钟之内释放 SDA. 每个半周期为 5
 * us, 大于标准模式的 tLOW 4.7 us 与 tHIGH 4.0 us (UM10204 表 11).
 */
#define I2C_CLEAR_CLOCKS	9
#define I2C_HALF_CLOCK_US	5

_Static_assert(I2C_TWBR >= 10, "datasheet 20.5.2: TWBR below 10 corrupts master output");
_Static_assert(I2C_TWBR <= 255, "TWBR is 8 bits, needs a TWI prescaler");

static void i2c_reset(void);

/*
 * held for a whole transfer, and while i2c_init() resets the bus
 * 整个传输期间持有, i2c_init() 复位总线时也持有
 */
static struct mutex bus_lock = MUTEX_INIT;

/**
 * i2c_init() - set up the TWI and reset the bus
 *
 * The internal pull-ups (20 to 50 kOhm, datasheet table 29-1) are weak
 * next to the backpack's own, but they keep both lines high while the
 * backpack is unplugged.  A floating bus can read as busy and leave
 * every START to time out; a pulled-up one answers with a fast NACK.
 *
 * Every PORTD and DDRD change in this file touches a single bit, which
 * avr-gcc emits as one sbi or cbi.  Other code may own other bits of
 * the same port, and an in/ori/out sequence here could be switched out
 * halfway and write back a stale bit.
 *
 * The reset would cut off a transfer another task has in flight, so it
 * runs under bus_lock.  panic() calls this too, possibly while the lock
 * is held, and skips the lock: nothing else runs after panic().
 *
 * 设置 TWI 并复位总线.
 *
 * 内部上拉 (20 至 50 kOhm, 数据手册表 29-1) 比转接板自带的上拉弱, 但能在转接板
 * 拔下时保持两条线为高电平. 悬空的总线可能被读成忙, 使每个 START 都超时; 有上拉
 * 的总线则很快以 NACK 应答.
 *
 * 本文件对 PORTD 与 DDRD 的每次修改只涉及一位, avr-gcc 为此生成一条 sbi 或 cbi.
 * 同一端口的其他位可能属于其他代码, 此处若使用 in/ori/out 序列, 任务可能在中途
 * 被切换出去, 再写回过时的位.
 *
 * 复位会打断另一个任务正在进行的传输, 因此在 bus_lock 下进行. panic() 也调用
 * i2c_init(), 此时锁可能已被持有, 因此 panic() 之下不取锁: panic() 之后不再有
 * 其他代码运行.
 *
 * Context: a task, or panic().
 * 任务中或 panic() 中调用.
 */
void i2c_init(void)
{
	if (!oops_in_progress)
		mutex_lock(&bus_lock);
	PORTD |= 1 << PORTD0;
	PORTD |= 1 << PORTD1;
	TWSR = 0;			/* prescaler 1; 预分频为 1 */
	TWBR = I2C_TWBR;
	i2c_reset();
	if (!oops_in_progress)
		mutex_unlock(&bus_lock);
}

/*
 * A wait has a jiffies deadline and a spin budget.  Normally the
 * deadline counts: the task can be switched out for two ticks or more
 * between any two instructions, so only the clock measures time.  After
 * panic() the tick is off and jiffies stands still, but nothing switches
 * the CPU away either, so counting I2C_SPIN_US steps measures it.
 *
 * 一次等待同时有 jiffies 期限与自旋次数. 通常以期限为准: 任意两条指令之间任务都
 * 可能被切换出去两个节拍以上, 只有时钟能度量时间. panic() 之后节拍停止, jiffies
 * 不再变化, 但也不会有任何切换, 因此按 I2C_SPIN_US 计数的步数可以度量时间.
 */
struct i2c_deadline {
	uint32_t jiffies;
	uint16_t spins;
};

static void i2c_deadline_start(struct i2c_deadline *d)
{
	d->jiffies = get_jiffies() + I2C_TIMEOUT_MS + 1;
	d->spins = I2C_TIMEOUT_MS * 1000UL / I2C_SPIN_US;
}

/*
 * True once @d has passed and @mask in TWCR still reads @busy.
 *
 * The clock is sampled before the register.  A transfer that finished
 * while the task was switched out is then seen as finished when TWCR is
 * read, not as a timeout.
 *
 * @d 已过期且 TWCR 中 @mask 位仍为 @busy 时为真. 先采样时钟, 再读寄存器. 这样,
 * 任务被切换出去期间已完成的传输, 在读 TWCR 时被看作已完成, 不会被当成超时.
 */
static int i2c_timed_out(struct i2c_deadline *d, uint8_t mask, uint8_t busy)
{
	int expired;

	if (oops_in_progress) {
		_delay_us(I2C_SPIN_US);
		expired = d->spins-- == 0;
	} else {
		expired = time_after_eq(get_jiffies(), d->jiffies);
	}
	return expired && (TWCR & mask) == busy;
}

/*
 * the status code with the prescaler bits masked, or -1
 * 屏蔽预分频位之后的状态码; 超时返回 -1
 */
static int i2c_wait(void)
{
	struct i2c_deadline deadline;

	i2c_deadline_start(&deadline);
	while (!(TWCR & (1 << TWINT)))
		if (i2c_timed_out(&deadline, 1 << TWINT, 0))
			return -1;
	return TW_STATUS;
}

/*
 * Open-drain by hand while the TWI is off: drive low with PORT 0 and
 * DDR 1, release with DDR 0 and PORT 1.  The order never passes through
 * DDR 1 with PORT 1, which would drive the line high against a slave.
 *
 * TWI 关闭期间手动实现开漏: PORT 0 且 DDR 1 时驱动为低, DDR 0 且 PORT 1 时释放.
 * 切换顺序不经过 DDR 1 且 PORT 1 的状态, 该状态会与从机对抗, 把线驱动为高.
 */
static void scl_low(void)
{
	PORTD &= ~(1 << PORTD0);
	DDRD |= 1 << DDD0;
}

static void scl_release(void)
{
	DDRD &= ~(1 << DDD0);
	PORTD |= 1 << PORTD0;
}

static void sda_low(void)
{
	PORTD &= ~(1 << PORTD1);
	DDRD |= 1 << DDD1;
}

static void sda_release(void)
{
	DDRD &= ~(1 << DDD1);
	PORTD |= 1 << PORTD1;
}

/*
 * Disabling TWEN drops whatever the hardware was doing and hands the
 * pins back to PORTD.  If a slave still holds SDA, clock it out, then
 * put a STOP on the bus so every slave returns to idle.
 *
 * Being switched out between edges only stretches a half clock, which
 * standard mode allows; it has no lower clock limit.
 *
 * 清除 TWEN 会丢弃硬件正在进行的操作, 把引脚交还给 PORTD. 若仍有从机拉住 SDA,
 * 就以时钟把从机的数据移出, 再在总线上发出 STOP, 使每个从机回到空闲.
 *
 * 在两个边沿之间被切换出去只会拉长半个时钟周期, 标准模式允许这种情况: 标准模式
 * 没有时钟频率下限.
 */
static void i2c_reset(void)
{
	uint8_t i;

	TWCR = 0;

	for (i = 0; i < I2C_CLEAR_CLOCKS && !(PIND & (1 << PIND1)); i++) {
		scl_low();
		_delay_us(I2C_HALF_CLOCK_US);
		scl_release();
		_delay_us(I2C_HALF_CLOCK_US);
	}

	/*
	 * STOP: SDA rises while SCL is high
	 * STOP: SCL 为高时 SDA 上升
	 */
	scl_low();
	sda_low();
	_delay_us(I2C_HALF_CLOCK_US);
	scl_release();
	_delay_us(I2C_HALF_CLOCK_US);
	sda_release();
	_delay_us(I2C_HALF_CLOCK_US);

	TWCR = 1 << TWEN;
}

/*
 * The hardware clears TWSTO once the STOP is on the bus.  A START
 * written to TWCR before that would replace the pending STOP.
 *
 * STOP 出现在总线上之后, 硬件才清除 TWSTO. 在此之前写入 TWCR 的 START 会取代尚
 * 未发出的 STOP.
 */
static void i2c_stop(void)
{
	struct i2c_deadline deadline;

	i2c_deadline_start(&deadline);
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
	while (TWCR & (1 << TWSTO))
		if (i2c_timed_out(&deadline, 1 << TWSTO, 1 << TWSTO)) {
			i2c_reset();
			return;
		}
}

/*
 * The polled transfer, for panic() only: interrupts are off there and
 * the scheduler must not be entered.  Same arguments and result as
 * i2c_write().
 *
 * 轮询方式的传输, 只供 panic() 使用: panic() 中中断已关闭, 且不得进入调度器. 参
 * 数与返回值同 i2c_write().
 */
static int i2c_write_polled(uint8_t addr, const uint8_t *buf, uint8_t len)
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	if (i2c_wait() != TW_START)
		goto fail;

	TWDR = (uint8_t)(addr << 1) | TW_WRITE;
	TWCR = (1 << TWINT) | (1 << TWEN);
	if (i2c_wait() != TW_MT_SLA_ACK)
		goto fail;

	while (len--) {
		TWDR = *buf++;
		TWCR = (1 << TWINT) | (1 << TWEN);
		if (i2c_wait() != TW_MT_DATA_ACK)
			goto fail;
	}

	i2c_stop();
	return 0;

fail:
	/*
	 * After a NACK the master still owns the bus and must release it.
	 * After a timeout or lost arbitration the state is unknown, and a
	 * reset is the only way back.
	 *
	 * NACK 之后主机仍占有总线, 必须释放总线. 超时或仲裁失败之后状态未知, 只
	 * 有复位才能恢复.
	 */
	if (TWCR & (1 << TWINT) && TW_STATUS != TW_MT_ARB_LOST)
		i2c_stop();
	else
		i2c_reset();
	return -1;
}

/*
 * The transfer in progress.  status stays I2C_BUSY until the interrupt
 * has put a STOP on the bus or met an error.  sla carries the R/W bit,
 * which selects whether buf is sent or filled.
 *
 * 正在进行的传输. 中断在总线上发出 STOP 或遇到错误之前, status 保持 I2C_BUSY.
 * sla 带有 R/W 位, 决定 buf 是被发送还是被填充.
 */
#define I2C_BUSY	1
#define I2C_DONE	0
#define I2C_FAILED	(-1)

static struct {
	uint8_t sla;
	uint8_t *buf;
	uint8_t len;
	volatile int8_t status;
} xfer;

static struct wait_queue xfer_wq = WAIT_QUEUE_INIT;

#define TWCR_GO		((1 << TWINT) | (1 << TWEN) | (1 << TWIE))
#define TWCR_STOP	((1 << TWINT) | (1 << TWSTO) | (1 << TWEN))

/*
 * acknowledge every byte but the last, so the slave stops sending
 * 除最后一个字节外都应答, 使从机停止发送
 */
static void twi_receive_next(void)
{
	TWCR = xfer.len > 1 ? TWCR_GO | (1 << TWEA) : TWCR_GO;
}

/**
 * twi_interrupt() - one step of the transfer per TWINT
 *
 * Status codes are those of the datasheet, tables 20-2 and 20-3.
 *
 * 每次 TWINT 推进一步传输. 状态码取自数据手册表 20-2 与表 20-3.
 *
 * Context: the TWI vector in switch.S, interrupts disabled.
 * 由 switch.S 中的 TWI 向量在关中断状态下调用.
 * Return: the number of tasks woken, so the vector knows whether to
 * switch.
 * 返回被唤醒的任务数, 中断向量据此决定是否切换任务.
 */
uint8_t twi_interrupt(void)
{
	switch (TW_STATUS) {
	case TW_START:
		TWDR = xfer.sla;
		TWCR = TWCR_GO;
		return 0;
	case TW_MT_SLA_ACK:
	case TW_MT_DATA_ACK:
		if (xfer.len) {
			TWDR = *xfer.buf++;
			xfer.len--;
			TWCR = TWCR_GO;
			return 0;
		}
		TWCR = TWCR_STOP;
		xfer.status = I2C_DONE;
		break;
	case TW_MR_SLA_ACK:
		twi_receive_next();
		return 0;
	case TW_MR_DATA_ACK:
		*xfer.buf++ = TWDR;
		xfer.len--;
		twi_receive_next();
		return 0;
	case TW_MR_DATA_NACK:
		*xfer.buf = TWDR;
		TWCR = TWCR_STOP;
		xfer.status = I2C_DONE;
		break;
	case TW_MT_ARB_LOST:
		/*
		 * another master owns the bus; a STOP is not ours to send
		 * 另一个主机占有总线, 不应由本机发出 STOP
		 */
		TWCR = (1 << TWINT) | (1 << TWEN);
		xfer.status = I2C_FAILED;
		break;
	default:
		/*
		 * NACK of the address or of a sent byte, or a bus error
		 * 地址或已发字节收到 NACK, 或总线错误
		 */
		TWCR = TWCR_STOP;
		xfer.status = I2C_FAILED;
		break;
	}
	return wake_up(&xfer_wq);
}

/*
 * Run one transfer to @sla, which carries the R/W bit, and sleep until
 * the interrupt finishes it.  Returns 0, or -1 on NACK, lost arbitration
 * or timeout.  After a timeout the bus is reset; the interrupt may still
 * fire once more, but by then no task waits on xfer_wq and the wake is
 * lost harmlessly.
 *
 * 向带有 R/W 位的 @sla 进行一次传输, 睡眠到中断完成传输. 成功时返回 0; NACK、
 * 仲裁失败或超时返回 -1. 超时之后复位总线; 中断可能还会再触发一次, 但那时已没有
 * 任务在 xfer_wq 上等待, 这次唤醒被丢弃, 不造成影响.
 */
static int i2c_transfer(uint8_t sla, uint8_t *buf, uint8_t len)
{
	struct i2c_deadline stop;
	int timed_out, ret = 0;

	mutex_lock(&bus_lock);

	/*
	 * the previous transfer's STOP may still be going out
	 * 上一次传输的 STOP 可能仍在发出
	 */
	i2c_deadline_start(&stop);
	while (TWCR & (1 << TWSTO))
		if (i2c_timed_out(&stop, 1 << TWSTO, 1 << TWSTO)) {
			i2c_reset();
			break;
		}

	xfer.sla = sla;
	xfer.buf = buf;
	xfer.len = len;
	xfer.status = I2C_BUSY;
	TWCR = TWCR_GO | (1 << TWSTA);

	/*
	 * each byte takes 90 us at 100 kHz; allow a millisecond each
	 * 100 kHz 下每字节需要 90 us; 每字节留出 1 ms
	 */
	timed_out = wait_event_timeout(&xfer_wq, xfer.status != I2C_BUSY,
				       I2C_TIMEOUT_MS + len);
	if (timed_out) {
		i2c_reset();
		ret = -1;
	} else if (xfer.status == I2C_FAILED) {
		ret = -1;
	}

	mutex_unlock(&bus_lock);
	return ret;
}

/**
 * i2c_write() - send bytes to a device
 * @addr: 7-bit address
 * @buf: the bytes
 * @len: how many; 0 only checks that a device answers at @addr
 *
 * 向 7 位地址 @addr 发送 @buf 中的 @len 字节. @len 可以为 0, 此时只检查 @addr
 * 处是否有设备应答.
 *
 * Context: a task after sched_start(), or panic().
 * 在 sched_start() 之后由任务调用, 或在 panic() 中调用.
 * Return: 0, or -1 on NACK, lost arbitration or timeout.
 * 成功时返回 0; NACK、仲裁失败或超时返回 -1.
 */
int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
	if (oops_in_progress)
		return i2c_write_polled(addr, buf, len);
	/*
	 * the interrupt only reads buf in this direction
	 * 这一方向上中断只读取 buf
	 */
	return i2c_transfer((uint8_t)(addr << 1) | TW_WRITE,
			    (uint8_t *)buf, len);
}

/**
 * i2c_read() - receive bytes from a device
 * @addr: 7-bit address
 * @buf: where to store them
 * @len: how many, at least one
 *
 * 从 7 位地址 @addr 接收 @len 字节 (至少一个) 到 @buf.
 *
 * Context: a task, after sched_start().
 * 在 sched_start() 之后由任务调用.
 * Return: 0, or -1 on NACK, lost arbitration, timeout, a zero @len, or
 * after panic(), which has no use for reads.
 * 成功时返回 0; NACK、仲裁失败、超时、@len 为 0, 或在 panic() 之后调用 (panic()
 * 不需要读取) 时返回 -1.
 */
int i2c_read(uint8_t addr, uint8_t *buf, uint8_t len)
{
	if (oops_in_progress || len == 0)
		return -1;
	return i2c_transfer((uint8_t)(addr << 1) | TW_READ, buf, len);
}
