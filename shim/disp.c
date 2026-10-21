/*
 * sunfish: keep a command-mode panel refreshing from the splash framebuffer.
 *
 * sweet's panel runs in video mode, so the MDP scans 0x9c000000 out on its
 * own and every CPU write shows up. The Pixel 4a's Samsung sofef01 is a DSI
 * command-mode panel: the MDP only sends a frame when CTL_START is kicked,
 * so after ABL's last frame the screen would freeze. Turning on the
 * pingpong block's autorefresh makes the hardware re-send the frame on every
 * tear-effect pulse, which is what the downstream SDE driver does instead of
 * CTL_START (sde_encoder_phys_cmd_trigger_start). Everything else (CTL,
 * layer mixer, DSC, DSI, TE) stays exactly as ABL configured it.
 *
 * Registers from the SM7150 DPU catalog (dpu_5_2_sm7150.h), MDP at 0xae01000.
 */
#include <stdint.h>
#include "console.h"
#include "disp.h"

#define MDP_BASE		0x0ae01000UL
#define PP_BASE(n)		(MDP_BASE + 0x70000UL + 0x800UL * (n))
#define PP_COUNT		4
#define PP_TEAR_CHECK_EN	0x000
#define PP_SYNC_CONFIG_VSYNC	0x004
#define PP_AUTOREFRESH_CONFIG	0x030
#define CTL_BASE(n)		(MDP_BASE + 0x1000UL + 0x200UL * (n))
#define CTL_COUNT		6
#define CTL_TOP			0x014
#define CTL_INTF_ACTIVE		0x0f4
/* Source pipes (VIG0, VIG1, DMA0-2): which buffer ABL's frame is fetched from */
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define AUTOREFRESH_EN		(1U << 31)
#define AUTOREFRESH_FRAMES	1	/* every TE pulse */

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

int disp_cmd_autorefresh(void)
{
	static const uint32_t sspp[] = { 0x4000, 0x6000, 0x24000, 0x26000, 0x28000 };
	int enabled = 0;

	/* Expect FB_BASE, stride FB_W * 4, size (FB_H << 16 | FB_W). */
	for (unsigned i = 0; i < sizeof(sspp) / sizeof(sspp[0]); i++) {
		uintptr_t p = MDP_BASE + sspp[i];
		uint32_t addr = rd32(p + SSPP_SRC0_ADDR);
		if (addr)
			con_hex2("DISP SRC", "addr=", addr, "size=", rd32(p + SSPP_SRC_SIZE));
		if (addr && addr != FB_BASE)
			con_hex2("DISP SRC", "NOT FB! stride=", rd32(p + SSPP_SRC_YSTRIDE0), "pipe=", sspp[i]);
	}
	for (int i = 0; i < CTL_COUNT; i++) {
		uint32_t top = rd32(CTL_BASE(i) + CTL_TOP);
		uint32_t intf = rd32(CTL_BASE(i) + CTL_INTF_ACTIVE);
		if (top || intf)
			con_hex2("DISP CTL", "top=", top, "intf=", intf | (uint32_t)i << 28);
	}
	for (int i = 0; i < PP_COUNT; i++) {
		uintptr_t pp = PP_BASE(i);
		uint32_t te = rd32(pp + PP_TEAR_CHECK_EN);
		if (!(te & 1))
			continue;	/* not driving a command-mode panel */
		uint32_t before = rd32(pp + PP_AUTOREFRESH_CONFIG);
		if (!(before & AUTOREFRESH_EN))
			wr32(pp + PP_AUTOREFRESH_CONFIG, AUTOREFRESH_EN | AUTOREFRESH_FRAMES);
		__asm__ volatile("dsb sy" ::: "memory");
		con_hex2("DISP PP", "n=", i, "ar was=", before);
		con_hex2("DISP PP", "ar now=", rd32(pp + PP_AUTOREFRESH_CONFIG),
			 "vsync=", rd32(pp + PP_SYNC_CONFIG_VSYNC));
		enabled++;
	}
	if (!enabled)
		con_str("DISP", "no tear-checked pingpong: video mode?");
	return enabled;
}
