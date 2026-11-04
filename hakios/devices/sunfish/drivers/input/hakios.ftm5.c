/*
 * The touch screen of the Pixel 4a (sunfish): an in-cell ST FTM5 at I2C address 0x49 on the SoC's serial engine 7, as a source
 * of touches for /dev/hakios-touch (sys/hakios.input.h). Only with boot-arg touchscreen=1. A thread reads it 100 times a second;
 * no interrupt is used.
 *
 * Boot-args: touchscreen=1 starts it; touch_delay=N waits N seconds first (default 45, as it ran: if a bus access were to hang
 * the SoC, the log and the screen got there first).
 *
 * Where the numbers come from: the phone's device tree and Google's msm-4.14 (touch at qup11, st,max-coords = <1079 2339>,
 * pinctrl touch_power_default), Google's fts_touch_s5 and mainline stmfts.c for the protocol (reset, CONTROLLER_READY, scan mode
 * {0xa0, 0x00, 0xff}, 8-byte events from 0x86), Linux i2c-qcom-geni.c and qcom-geni-se.c for the engine in FIFO mode.
 *
 * It needs the engine's I2C firmware to be loaded already: ABL has done that. Without it (protocol not 3) one line says so and
 * the driver stops, and no firmware is ever loaded here. The rails (L13A, L7C) are the panel's and are left as they are.
 *
 * ONE WRITE TO A PMIC: the touch controller's load switch, PM6150 GPIO4 (SPMI slave 0, 0xc340..0xc346), and only when the
 * switch is not already on. Nothing else in a PMIC is written. Channels are checked to belong to us (EE 0), and a channel that
 * times out is not used again. One transfer at a time with the other SPMI drivers (hakios_spmi_lock()).
 *
 * Status: this logic ran on the phone as bsd/dev/sweet_touch.c on the patched XNU of ios-sunfish (multitouch used daily, booted
 * by ABL through the shim). THIS FILE: compiled only, not yet run; not yet tried on a boot through U-Boot, which may leave the
 * engine, its clocks or the switch differently.
 */
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void hakios_spmi_lock(void);
extern void hakios_spmi_unlock(void);

#define TOUCH_SE_PA      0x00a84000u
#define TOUCH_GCC_PA     0x00100000u
#define TOUCH_NORTH_PA   0x03900000u /* DT TLMM + NORTH (0x500000) */
#define TOUCH_GCC_VOTE   0x5200cu
#define TOUCH_RCG       0x18148u    /* wrapper1 LOCAL S1 = global SE7 */
#define TOUCH_GPIO_CFG(n) ((uint32_t)(n) * 0x1000u)
#define TOUCH_GPIO_IO(n)  (TOUCH_GPIO_CFG(n) + 4u)
#define TOUCH_MAX        10u
/* ST FTM5, I2C protocol of fts_touch_s5/fts_lib/ftsHardware.h */
#define FTM5_ADDR        0x49u
#define FTM5_READ_EVENT  0x86u      /* FIFO_CMD_READALL: one 8-byte event per read */
#define FTM5_SCAN_MODE   0xa0u
#define FTM5_EV_SIZE     8u
#define FTM5_EV_READY    0x03u
#define FTM5_EV_ENTER    0x13u
#define FTM5_EV_MOTION   0x23u
#define FTM5_EV_LEAVE    0x33u
#define FTM5_EV_ERROR    0xf3u
#define FTM5_MAX_X       1079u      /* downstream st,max-coords = <1079 2339> */
#define FTM5_MAX_Y       2339u
#define PM6150_GPIO4     0xc300u    /* SPMI SID 0 */

/* The PMIC arbiter, version 5: Linux spmi-pmic-arb.c, the same as drivers/input/hakios.buttons.c */
#define SP_CORE_PA       0x0c440000u
#define SP_OBS_PA        0x0e600000u
#define SP_CFG_PA        0x0c40a000u
#define SP_CHNL_PA       0x0c600000u /* EE 0's write channels, 0x10000 each */
#define SP_NO_APID       0xffffu

#define SE_STATUS       0x040u
#define SE_MCLK         0x048u
#define SE_FW           0x068u
#define SE_CLK_SEL      0x07cu
#define SE_DMA          0x258u
#define SE_TX_LEN       0x26cu
#define SE_RX_LEN       0x270u
#define SE_SCL          0x278u
#define SE_CMD          0x600u
#define SE_CTRL         0x604u
#define SE_IRQ          0x610u
#define SE_IRQ_EN       0x614u
#define SE_IRQ_CLR      0x618u
#define SE_TX           0x700u
#define SE_RX           0x780u
#define SE_TX_COUNT     0x800u
#define SE_RX_COUNT     0x804u
#define SE_HW_TX        0xe24u
#define SE_HW_RX        0xe28u
#define SE_TX_WM        0x80cu
#define SE_RX_WM_IRQ    (1u << 26)
#define SE_RX_LAST      (1u << 27)
#define SE_TX_WM_IRQ    (1u << 30)
#define SE_DONE         (1u << 0)
#define SE_NACK         (1u << 10)
#define SE_ERRORS       (0x3eu | (1u << 9) | (1u << 10) | (1u << 12) | \
                         (1u << 13) | (3u << 24) | (3u << 28))
#define SE_EVENTS       (SE_DONE | SE_ERRORS | SE_RX_WM_IRQ | SE_RX_LAST | SE_TX_WM_IRQ)
/* geni_se_config_packing(8, 4, true, true, true): start bits 7/15/23/31,
 * MSB-to-LSB, length 8, STOP only on vector 3. FIFO bytes remain LSB first. */
#define SE_PACK0        0x0007f8feu
#define SE_PACK1        0x000ffefeu

static struct {
	vm_offset_t se, gcc, gpio;
	bool dead, probing;
	uint32_t irq_status, irq_last, active;
	unsigned sent, got;
	int x[TOUCH_MAX], y[TOUCH_MAX];
} st;
static int touch_sleep_chan;

static uint32_t
touch_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
touch_wr(vm_offset_t base, uint32_t off, uint32_t val)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = val;
	__asm__ volatile("dsb osh" ::: "memory");
}

static void
touch_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&touch_sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static bool
touch_wait_clear(vm_offset_t base, uint32_t off, uint32_t mask)
{
	for (unsigned i = 0; i < 5000; ++i) {
		if (!(touch_rd(base, off) & mask)) {
			return true;
		}
		delay(1);
	}
	return false;
}

static void
touch_gpio(unsigned pin, unsigned mux, unsigned pull, unsigned drive, bool out, bool high)
{
	uint32_t off = TOUCH_GPIO_CFG(pin);
	uint32_t cfg = touch_rd(st.gpio, off);
	if (out) {
		/* Set the desired latch before enabling output (reset is active low). */
		touch_wr(st.gpio, TOUCH_GPIO_IO(pin),
		    (touch_rd(st.gpio, TOUCH_GPIO_IO(pin)) & ~2u) | (high ? 2u : 0u));
	}
	cfg &= ~0x3ffu;
	cfg |= (mux << 2) | pull | (drive << 6) | (out ? 0x200u : 0u);
	touch_wr(st.gpio, off, cfg);
}

static bool
touch_clocks(void)
{
	/* Vote on only wrapper1 and SE7, preserving other clock users. */
	uint32_t mask = (0x0fu << 18) | (1u << 23);
	touch_wr(st.gcc, TOUCH_GCC_VOTE, touch_rd(st.gcc, TOUCH_GCC_VOTE) | mask);
	uint32_t dfs = touch_rd(st.gcc, TOUCH_RCG + 0x14u);
	uint32_t cfg = touch_rd(st.gcc, TOUCH_RCG + 4u);
	printf("ftm5: gcc vote=%#x rcg=%#x cfg=%#x dfs=%#x\n",
	    touch_rd(st.gcc, TOUCH_GCC_VOTE), touch_rd(st.gcc, TOUCH_RCG), cfg, dfs);
	if (dfs & 1u) {
		/* SE CLK_SEL=0 selects DFS performance level 0. Do not alter tables. */
		cfg = touch_rd(st.gcc, TOUCH_RCG + 0x1cu);
		if ((cfg & 0x371fu) != 1u) {
			printf("ftm5: DFS level0=%#x is not XO/1; need clock integration\n", cfg);
			return false;
		}
	} else {
		/* XO parent 0, HID divider 1 => 19.2 MHz. No PLL or voltage change. */
		cfg = (cfg & ~(0x371fu | (1u << 20))) | 1u;
		touch_wr(st.gcc, TOUCH_RCG + 4u, cfg);
		touch_wr(st.gcc, TOUCH_RCG, touch_rd(st.gcc, TOUCH_RCG) | 1u);
		if (!touch_wait_clear(st.gcc, TOUCH_RCG, 1u)) {
			printf("ftm5: RCG update timeout\n");
			return false;
		}
	}
	const uint32_t branches[] = {0x18004, 0x18008, 0x1800c, 0x18010, 0x18144};
	for (unsigned i = 0; i < sizeof(branches) / sizeof(branches[0]); ++i) {
		if (!touch_wait_clear(st.gcc, branches[i], 1u << 31)) {
			printf("ftm5: GCC branch %#x halted (%#x)\n", branches[i],
			    touch_rd(st.gcc, branches[i]));
			return false;
		}
	}
	return true;
}

/* Called only for a transfer started by this driver, including a stretched TX. */
static void
touch_abort(void)
{
	touch_wr(st.se, SE_CTRL, 2); /* abort M command, release held bus */
	bool done = false;
	for (unsigned i = 0; i < 5000; ++i) {
		if (touch_rd(st.se, SE_IRQ) & (1u << 5)) {
			done = true;
			break;
		}
		delay(1);
	}
	if (!done || !touch_wait_clear(st.se, SE_STATUS, 1u)) {
		st.dead = true;
		printf("ftm5: abort failed status=%#x irq=%#x; stopping\n",
		    touch_rd(st.se, SE_STATUS), touch_rd(st.se, SE_IRQ));
	}
	/* Bound drain even if the hardware reports an impossible FIFO count. */
	for (unsigned i = 0; i < 64 && (touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu); ++i) {
		(void)touch_rd(st.se, SE_RX);
	}
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	if (touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) {
		st.dead = true;
		printf("ftm5: TX FIFO not empty after abort; stopping\n");
	}
}

/* Keep all observed bits for diagnostics, even if a watermark was cleared. */
static uint32_t
touch_irq(void)
{
	st.irq_last = touch_rd(st.se, SE_IRQ);
	st.irq_status |= st.irq_last;
	return st.irq_last;
}

/* One Linux-style FIFO message. TX is at most 3 bytes; RX at most 90. */
static int
touch_msg_run(uint8_t addr, const uint8_t *tx, unsigned nt, uint8_t *rx,
    unsigned nr, bool stretch)
{
	if (st.dead || nt > 3 || nr > 90 || (nt && nr) || (!nt && !nr)) {
		return EIO;
	}
	if (!touch_wait_clear(st.se, SE_STATUS, 1u)) {
		st.dead = true; /* no other SE7 owner is supported */
		return EBUSY;
	}
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	touch_wr(st.se, SE_TX_WM, nt ? 1u : 0u);
	touch_wr(st.se, nr ? SE_RX_LEN : SE_TX_LEN, nr ? nr : nt);
	touch_wr(st.se, SE_CMD, ((nr ? 2u : 1u) << 27) |
	    ((uint32_t)addr << 9) | (stretch ? 4u : 0u));
	int err = ETIMEDOUT;
	for (unsigned i = 0; i < 20000; ++i) {
		uint32_t irq = touch_irq();
		if (irq & SE_ERRORS) {
			err = (irq & SE_NACK) ? ENXIO : EIO;
			/* A completed NACK is normal during address probing. Aborting an
			 * idle sequencer may never produce ABORT_DONE. Wait for completion
			 * and leave a clean idle engine available for the other IC. */
			for (unsigned j = 0; j < 5000 && !(irq & SE_DONE); ++j) {
				delay(1);
				irq = touch_irq();
			}
			if ((irq & SE_DONE) && !(touch_rd(st.se, SE_STATUS) & 1u) &&
			    !(touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) &&
			    !(touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu)) {
				touch_wr(st.se, SE_IRQ_CLR, irq);
				return err;
			}
			break;
		}
		if (nt && !st.sent && (irq & SE_TX_WM_IRQ)) {
			uint32_t word = 0;
			for (unsigned j = 0; j < nt; ++j) {
				word |= (uint32_t)tx[j] << (8u * j);
			}
			touch_wr(st.se, SE_TX, word);
			st.sent = nt;
			touch_wr(st.se, SE_TX_WM, 0);
		}
		uint32_t count = touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu;
		if (count > 64 || count > (nr - st.got + 3u) / 4u) {
			err = EIO;
			break;
		}
		while (count--) {
			uint32_t word = touch_rd(st.se, SE_RX);
			for (unsigned j = 0; j < 4 && st.got < nr; ++j) {
				rx[st.got++] = (uint8_t)(word >> (8u * j));
			}
		}
		/* DONE is checked after draining RX, so final short words aren't lost. */
		if (irq & SE_DONE) {
			/* Re-sample after the final drain; a late error must beat DONE. */
			irq = touch_irq();
			if (irq & SE_ERRORS) {
				continue;
			}
			touch_wr(st.se, SE_IRQ_CLR, irq);
			if (st.got == nr && st.sent == nt) {
				return 0;
			}
			err = EIO;
			break;
		}
		/* Preserve DONE/errors if they arrive during this iteration. */
		touch_wr(st.se, SE_IRQ_CLR, irq & (SE_RX_WM_IRQ | SE_RX_LAST | SE_TX_WM_IRQ));
		delay(1);
	}
	touch_abort();
	return err;
}

static int
touch_msg(uint8_t addr, const uint8_t *tx, unsigned nt, uint8_t *rx,
    unsigned nr, bool stretch)
{
	st.irq_status = st.irq_last = 0;
	st.sent = st.got = 0;
	int err = touch_msg_run(addr, tx, nt, rx, nr, stretch);
	if (st.probing) {
		printf("ftm5: probe xfer addr=%#x %s len=%u stretch=%u err=%d irq=%#x seen=%#x tx=%u rx=%u status=%#x\n",
		    addr, nr ? "read" : "write", nr ? nr : nt, stretch ? 1u : 0u,
		    err, st.irq_last, st.irq_status, st.sent, st.got,
		    touch_rd(st.se, SE_STATUS));
	}
	return err;
}

static int
touch_read(uint8_t addr, uint16_t reg, bool wide, uint8_t *buf, unsigned len)
{
	uint8_t ptr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
	int err = touch_msg(addr, wide ? ptr : ptr + 1, wide ? 2u : 1u,
	    NULL, 0, true);
	if (err) {
		return err;
	}
	/* STOP_STRETCH on the pointer write retains the bus for a repeated START. */
	return touch_msg(addr, NULL, 0, buf, len, false);
}

static vm_offset_t sp_core, sp_obs, sp_cfg, sp_chnl;
static uint16_t sp_apid = SP_NO_APID;
static unsigned sp_count;
static bool sp_poisoned;

/* The channel of PM6150 GPIO4, and that it is ours. */
static bool
sp_init(void)
{
	sp_core = ml_io_map(SP_CORE_PA, 0x2000);
	sp_cfg = ml_io_map(SP_CFG_PA, 0x1000);
	sp_obs = ml_io_map(SP_OBS_PA, 0x10000);
	if (!sp_core || !sp_cfg || !sp_obs) {
		printf("ftm5: could not map the PMIC arbiter\n");
		return false;
	}
	uint32_t version = touch_rd(sp_core, 0);
	sp_count = touch_rd(sp_core, 4) & 0x7ffu;
	if (version < 0x50000000u || version >= 0x70000000u || !sp_count || sp_count > 512) {
		printf("ftm5: PMIC arbiter version %#x, %u channels: not one this driver knows\n", version, sp_count);
		return false;
	}
	for (unsigned i = 0; i < sp_count; ++i) {
		uint32_t map = touch_rd(sp_core, 0x900u + 4u * i);
		if (!map || ((map >> 8) & 0xfffu) != (PM6150_GPIO4 >> 8)) {
			continue;
		}
		if (sp_apid == SP_NO_APID || (touch_rd(sp_cfg, 0x700u + 4u * i) & 7u) == 0) {
			sp_apid = (uint16_t)i;
		}
	}
	if (sp_apid == SP_NO_APID) {
		printf("ftm5: the arbiter has no channel for PM6150 GPIO4\n");
		return false;
	}
	return true;
}

static int
sp_read(unsigned reg, uint8_t *out)
{
	unsigned off = 0x80u * sp_apid;
	int result = -1;

	if (sp_poisoned || reg < 0x40u || reg > 0x46u) {
		return -1;
	}
	hakios_spmi_lock();
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(sp_obs + off) = (1u << 27) | (reg << 4); /* EXT_READL, one byte */
	__asm__ volatile("dsb osh" ::: "memory");
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = touch_rd(sp_obs, off + 0x08u);
		if (status & 1u) {
			if (!(status & 0x0eu)) {
				*out = (uint8_t)touch_rd(sp_obs, off + 0x18u);
				result = 0;
			}
			hakios_spmi_unlock();
			return result;
		}
		delay(1);
	}
	sp_poisoned = true;
	hakios_spmi_unlock();
	printf("ftm5: PMIC read of %#x timed out; the channel is not used again\n", reg);
	return -1;
}

/* Only the switch's own registers, and only on a channel of ours. */
static int
sp_write(unsigned reg, uint8_t value)
{
	if (sp_poisoned || reg < 0x40u || reg > 0x46u || (touch_rd(sp_cfg, 0x700u + 4u * sp_apid) & 7u) != 0) {
		printf("ftm5: write of PM6150 GPIO4 %#x refused\n", reg);
		return -1;
	}
	if (!sp_chnl) {
		sp_chnl = ml_io_map(SP_CHNL_PA + 0x10000u * sp_apid, 0x1000); /* not under the spin lock */
		if (!sp_chnl) {
			return -1;
		}
	}
	hakios_spmi_lock();
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(sp_chnl + 0x10u) = value;    /* WDATA0 */
	*(volatile uint32_t *)(sp_chnl + 0x00u) = reg << 4; /* EXT_WRITEL, one byte */
	__asm__ volatile("dsb osh" ::: "memory");
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = touch_rd(sp_chnl, 0x08u);
		if (status & 1u) {
			hakios_spmi_unlock();
			return (status & 0x0eu) ? -1 : 0;
		}
		delay(1);
	}
	sp_poisoned = true;
	hakios_spmi_unlock();
	printf("ftm5: PMIC write of %#x timed out; the channel is not used again\n", reg);
	return -1;
}

/* 0..1079, 0..2339 on the panel to /dev/hakios-touch's 0..65535 */
static void
touch_post(uint32_t type, unsigned id, int x, int y)
{
	hakios_touch_post(type, id, (uint32_t)x * 65535u / FTM5_MAX_X, (uint32_t)y * 65535u / FTM5_MAX_Y);
}

static void
touch_report(uint32_t mask, const int *x, const int *y)
{
	for (unsigned id = 0; id < TOUCH_MAX; ++id) {
		uint32_t bit = 1u << id;
		if (mask & bit) {
			if (!(st.active & bit)) {
				touch_post(HAKIOS_TOUCH_DOWN, id, x[id], y[id]);
			} else if (x[id] != st.x[id] || y[id] != st.y[id]) {
				touch_post(HAKIOS_TOUCH_MOVE, id, x[id], y[id]);
			}
			st.x[id] = x[id];
			st.y[id] = y[id];
		} else if (st.active & bit) {
			touch_post(HAKIOS_TOUCH_UP, id, st.x[id], st.y[id]);
		}
	}
	st.active = mask;
}

static int
touch_ftm5_event(uint8_t *ev)
{
	return touch_read(FTM5_ADDR, FTM5_READ_EVENT, false, ev, FTM5_EV_SIZE);
}


/* PM6150 GPIO4 -> digital output, high, VIN1, no pull, 2 mA, enabled. */
static bool
touch_ftm5_power(void)
{
	static const struct {
		uint16_t reg;
		uint8_t val;
	} seq[] = {
		{0x41, 0x01}, /* DIG_VIN_CTL: power-source = <1> */
		{0x42, 0x05}, /* DIG_PULL_CTL: bias-disable */
		{0x45, 0x02}, /* DIG_OUT_CTL: CMOS, qcom,drive-strength = <2> */
		{0x44, 0x80}, /* LV_MV_DIG_OUT_SOURCE_CTL: "normal", output high */
		{0x40, 0x01}, /* MODE_CTL: digital output */
		{0x46, 0x80}, /* EN_CTL: enable */
	};
	uint8_t mode = 0xff, src = 0xff, en = 0xff;
	if (!sp_init()) {
		printf("ftm5: no SPMI; cannot check the FTM5 load switch\n");
		return false;
	}
	if (sp_read(0x40u, &mode) || sp_read(0x44u, &src) || sp_read(0x46u, &en)) {
		printf("ftm5: cannot read the load switch; not writing to it\n");
		return false;
	}
	printf("ftm5: PM6150 GPIO4 mode=%#x src=%#x en=%#x\n", mode, src, en);
	if ((mode & 3u) == 1u && (src & 0x80u) && (en & 0x80u)) {
		return true; /* ABL already switched the touch rails on */
	}
	for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
		if (sp_write(seq[i].reg, seq[i].val) != 0) {
			printf("ftm5: PM6150 GPIO4 write %#x failed\n", seq[i].reg);
			return false;
		}
	}
	return true;
}

/* Bring-up of the sunfish FTM5. Returns true when sensing is on. */
static bool
touch_ftm5_start(void)
{
	uint8_t ev[FTM5_EV_SIZE];
	printf("ftm5: sunfish FTM5: TLMM reset=%#x irq=%#x switch=%#x\n",
	    touch_rd(st.gpio, TOUCH_GPIO_IO(8)), touch_rd(st.gpio, TOUCH_GPIO_IO(9)),
	    touch_rd(st.gpio, TOUCH_GPIO_IO(72)));
	touch_gpio(9, 0, 0, 0, false, false);  /* IRQ input, no pull (polled) */
	touch_gpio(72, 0, 0, 0, true, false);  /* IRQ routed to the AP, not SLPI */
	touch_gpio(8, 0, 0, 0, true, false);   /* hold in reset while powering */
	if (!touch_ftm5_power()) {
		return false;
	}
	touch_wait(10);
	touch_gpio(6, 1, 0, 0, false, false);  /* qup11 SDA/SCL */
	touch_gpio(7, 1, 0, 0, false, false);
	/* fts_system_reset() with a reset GPIO: low 10 ms, high, poll for READY. */
	touch_gpio(8, 0, 0, 0, true, true);
	touch_wait(10);
	st.probing = true;
	int err = ENXIO;
	bool ready = false;
	for (unsigned i = 0; i < 40 && !ready; ++i) {
		touch_wait(25);
		memset(ev, 0, sizeof(ev));
		err = touch_ftm5_event(ev);
		if (!err && ev[0] == FTM5_EV_READY) {
			ready = true;
		}
		st.probing = i < 2; /* log the first transfers only */
	}
	st.probing = false;
	printf("ftm5: FTM5 0x49 err=%d ready=%d event=%02x %02x %02x %02x %02x %02x %02x %02x\n",
	    err, ready, ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
	if (err || st.dead) {
		return false;
	}
	/* Even without a READY event (missed, or reset already done) the IC answers. */
	const uint8_t scan[3] = {FTM5_SCAN_MODE, 0x00, 0xff};
	err = touch_msg(FTM5_ADDR, scan, sizeof(scan), NULL, 0, false);
	printf("ftm5: FTM5 scan mode active err=%d\n", err);
	return err == 0;
}

/*
 * Drain the FTM5 event FIFO and report contacts. Each 8-byte read pops one
 * event; byte 7 bits 0-4 count the events still queued (fts_event_handler).
 */
static int
touch_ftm5(void)
{
	int x[TOUCH_MAX], y[TOUCH_MAX];
	uint32_t mask = st.active;
	uint8_t ev[FTM5_EV_SIZE];
	for (unsigned id = 0; id < TOUCH_MAX; ++id) {
		x[id] = st.x[id];
		y[id] = st.y[id];
	}
	for (unsigned n = 0; n < 32; ++n) {
		int err = touch_ftm5_event(ev);
		if (err) {
			return err;
		}
		if (ev[0] == 0x00) {
			break; /* FIFO empty */
		}
		/* Bits 2-3 of byte 0 carry touch-major bits (mainline STMFTS5_MASK_EVENT_ID). */
		uint8_t kind = ev[0] & 0xf3u;
		unsigned id = ev[1] >> 4;
		if (kind == FTM5_EV_ENTER || kind == FTM5_EV_MOTION) {
			unsigned px = ((unsigned)(ev[3] & 15u) << 8) | ev[2];
			unsigned py = ((unsigned)ev[4] << 4) | (ev[3] >> 4);
			if (id < TOUCH_MAX && px <= FTM5_MAX_X && py <= FTM5_MAX_Y) {
				mask |= 1u << id;
				x[id] = (int)px;
				y[id] = (int)py;
			}
		} else if (kind == FTM5_EV_LEAVE) {
			if (id < TOUCH_MAX) {
				mask &= ~(1u << id);
			}
		} else if (kind == FTM5_EV_ERROR) {
			printf("ftm5: FTM5 error event %02x %02x %02x %02x %02x %02x %02x\n",
			    ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
		}
		/* status, user report, debug events: ignored */
		if ((ev[7] & 0x1fu) == 0) {
			break;
		}
	}
	touch_report(mask, x, y);
	return 0;
}

static bool
touch_se_init(void)
{
	uint32_t fw = touch_rd(st.se, SE_FW);
	uint32_t tx = touch_rd(st.se, SE_HW_TX), rx = touch_rd(st.se, SE_HW_RX);
	printf("ftm5: se7 fw=%#x proto=%u status=%#x tx=%#x rx=%#x ios=%#x\n",
	    fw, (fw >> 8) & 0xffu, touch_rd(st.se, SE_STATUS), tx, rx,
	    touch_rd(st.se, 0x908));
	if (((fw >> 8) & 0xffu) != 3u || (touch_rd(st.se, 0x64) & 1u) ||
	    ((tx >> 24) & 0x3fu) != 32u || ((rx >> 24) & 0x3fu) != 32u ||
	    ((tx >> 16) & 0x3fu) < 2u || ((rx >> 16) & 0x3fu) < 2u) {
		printf("ftm5: need ABL-loaded I2C firmware and 32-bit FIFOs; probes skipped\n");
		return false;
	}
	if (touch_rd(st.se, SE_STATUS) & 0x1001u) {
		printf("ftm5: SE7 already busy; probes skipped\n");
		return false;
	}
	touch_wr(st.se, 0xe18, 0); /* no GSI events */
	touch_wr(st.se, 0xe1c, 0); /* mask external IRQ routing; poll local status */
	touch_wr(st.se, 0x644, 0);
	touch_wr(st.se, SE_IRQ_CLR, 0xffffffffu);
	touch_wr(st.se, 0x648, 0xffffffffu);
	touch_wr(st.se, SE_DMA, 0);
	touch_wr(st.se, 0x28, touch_rd(st.se, 0x28) | 0x7fu);
	touch_wr(st.se, 0xe30, touch_rd(st.se, 0xe30) | 0x0fu);
	touch_wr(st.se, 0x24, 0x7f);
	touch_wr(st.se, 0x20, 1);
	touch_wr(st.se, SE_CLK_SEL, 0);
	touch_wr(st.se, SE_MCLK, (7u << 4) | 1u);
	touch_wr(st.se, SE_SCL, (10u << 20) | (11u << 10) | 26u); /* 100 kHz */
	/* Linux 8-bit I2C packing: MSB on the wire, LSB byte first in FIFO. */
	touch_wr(st.se, 0x254, 0);
	touch_wr(st.se, 0x260, SE_PACK0);
	touch_wr(st.se, 0x264, SE_PACK1);
	touch_wr(st.se, 0x284, SE_PACK0);
	touch_wr(st.se, 0x288, SE_PACK1);
	touch_wr(st.se, SE_TX_WM, 0);
	touch_wr(st.se, 0x810, 0);
	touch_wr(st.se, 0x814, ((rx >> 16) & 0x3fu) - 1u);
	touch_wr(st.se, SE_IRQ_EN, SE_EVENTS);
	printf("ftm5: packing tx=%#x/%#x rx=%#x/%#x gran=%#x\n",
	    touch_rd(st.se, 0x260), touch_rd(st.se, 0x264),
	    touch_rd(st.se, 0x284), touch_rd(st.se, 0x288), touch_rd(st.se, 0x254));
	if (touch_rd(st.se, 0x260) != SE_PACK0 || touch_rd(st.se, 0x264) != SE_PACK1 ||
	    touch_rd(st.se, 0x284) != SE_PACK0 || touch_rd(st.se, 0x288) != SE_PACK1 ||
	    touch_rd(st.se, 0x254) != 0) {
		printf("ftm5: packing readback mismatch; probes skipped\n");
		return false;
	}
	if ((touch_rd(st.se, SE_TX_COUNT) & 0xfffffffu) ||
	    (touch_rd(st.se, SE_RX_COUNT) & 0x1ffffffu)) {
		printf("ftm5: inherited FIFO data; probes skipped\n");
		return false;
	}
	return true;
}


static void
touch_thread(void *arg, wait_result_t result)
{
	unsigned delay_s = 45, errors = 0;

	(void)arg;
	(void)result;
	PE_parse_boot_argn("touch_delay", &delay_s, sizeof(delay_s));
	printf("ftm5: starting in %u s\n", delay_s);
	touch_wait(delay_s * 1000u);
	st.gcc = ml_io_map(TOUCH_GCC_PA, 0x60000);
	st.gpio = ml_io_map(TOUCH_NORTH_PA, 0x5b000);
	st.se = ml_io_map(TOUCH_SE_PA, 0x4000);
	if (!st.gcc || !st.gpio || !st.se) {
		printf("ftm5: could not map GCC, TLMM or the serial engine\n");
		goto out;
	}
	if (!touch_clocks() || !touch_se_init()) {
		goto out;
	}
	if (!touch_ftm5_start() || st.dead) {
		printf("ftm5: bring-up failed; stopping\n");
		goto out;
	}
	printf("ftm5: running, 1080x2340, every 10 ms\n");
	while (!st.dead) {
		int err = touch_ftm5();
		if (err) {
			if ((errors++ % 100u) == 0) {
				printf("ftm5: report err=%d irq=%#x\n", err, st.irq_status);
			}
			if (errors == 10) {
				touch_report(0, st.x, st.y); /* release after persistent bad reads */
			}
		} else {
			errors = 0;
		}
		touch_wait(10);
	}
	touch_report(0, st.x, st.y);
out:
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
ftm5_init(void)
{
	uint32_t enabled = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("touchscreen", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	if (kernel_thread_start(touch_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, ftm5_init);
