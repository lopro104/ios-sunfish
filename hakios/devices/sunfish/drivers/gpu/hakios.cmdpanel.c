/*
 * The Pixel 4a's panel takes frames only when it is sent them: it is a DSI command-mode panel, where a video-mode panel (the POCO
 * X5 Pro's) is scanned out continuously. iOS's framebuffer here (MiniIOMobileFramebuffer) only writes into the memory the display
 * engine reads, so without this the screen stays on the last frame sent. With boot-arg panel_autorefresh=1 this turns on the
 * autorefresh of each pingpong block doing tear checks: the hardware then sends the frame on every tear-effect pulse from the panel.
 *
 * Where the numbers come from: the SM7150 DPU catalog (Linux dpu_5_2_sm7150.h): MDP at 0xae01000, pingpong n at +0x70000 + 0x800 n,
 * TEAR_CHECK_EN +0x000, AUTOREFRESH_CONFIG +0x030 (BIT(31) on, and every how many pulses).
 *
 * The display engine has to be on and clocked when this runs, or touching it can stop the phone. ABL leaves it on. That is why it
 * needs its boot-argument. No check here can tell in advance. What U-Boot leaves is not known yet.
 *
 * Writes to no PMIC, and only to this one register of the display engine. If autorefresh does not take, one line says so and
 * nothing else is tried (the old tree's fallback, CTL_START after each frame, needs a hook in the framebuffer's frame loop).
 *
 * Status: these writes ran on the phone from shim/disp.c and SweetFramebuffer.cpp of ios-sunfish, booted by ABL. THIS FILE: compiled
 * only, not yet run; not tried on a boot through U-Boot.
 */
#include <kern/startup.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define MDP_BASE                0x0ae01000UL
#define MDP_SIZE                0x72000UL
#define MDP_PP(n)               (0x70000u + 0x800u * (n))
#define PP_TEAR_CHECK_EN        0x000u
#define PP_AUTOREFRESH          0x030u
#define AUTOREFRESH_ON          0x80000001u     /* BIT(31), every pulse */

static void
cmdpanel_init(void)
{
	uint32_t enabled = 0;
	unsigned found = 0;
	vm_offset_t mdp;

	if (!PE_parse_boot_argn("panel_autorefresh", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	mdp = ml_io_map(MDP_BASE, MDP_SIZE);
	if (mdp == 0) {
		printf("cmdpanel: could not map the display engine\n");
		return;
	}
	for (unsigned pp = 0; pp < 4; pp++) {
		volatile uint32_t *block = (volatile uint32_t *)(mdp + MDP_PP(pp));
		uint32_t was, now;

		if (!(block[PP_TEAR_CHECK_EN / 4] & 1u)) {
			continue;       /* not driving a command-mode panel */
		}
		found++;
		was = block[PP_AUTOREFRESH / 4];
		if (!(was & 0x80000000u)) {
			block[PP_AUTOREFRESH / 4] = AUTOREFRESH_ON;
			__asm__ volatile ("dsb sy" ::: "memory");
		}
		now = block[PP_AUTOREFRESH / 4];
		printf("cmdpanel: pingpong %u: autorefresh %#x -> %#x%s\n", pp, was, now,
		    (now & 0x80000000u) ? "" : " (DID NOT TAKE: the screen will not follow)");
	}
	if (found == 0) {
		printf("cmdpanel: no pingpong does tear checks: the panel was not left in command mode, nothing done\n");
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, cmdpanel_init);
