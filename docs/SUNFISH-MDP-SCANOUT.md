# SM7150 (sdmmagpie) MDP: scan out IOSurfaces directly, and scale them

Offline research, 2026-10-27. Nothing was run on the phone. Tags: **[src]** means read in the source, **[obs]** means seen on the phone before (NOTES.md), **[guess]** means not verified.

Abbreviations used below:
- DS = downstream msm-4.14, `~/EvolutionX/kernel/google/msm-4.14`
- ML = mainline, `~/EvolutionX/kernel/mainline/sm7150-mainline`
- `dpu1/` = `drivers/gpu/drm/msm/disp/dpu1/`
- `sde/` = `drivers/gpu/drm/msm/sde/`. In this tree it is not under techpack.

Addresses. MDSS is at 0xae00000 and MDP at 0xae01000. The DS dtsi gives offsets from MDSS. ML and our code (`MDP_BASE`) give offsets from MDP. So DS 0x5000 and ML 0x4000 are the same place, 0xae05000.

## 0. The main blocker: XNU memory cannot be reached with physical addresses

- The fetch address is 32 bits wide: `uint32_t plane_addr[]` (DS `sde/sde_hw_mdss.h:473`). SSPP_SRC0..3_ADDR (0x14..0x20) are single 32-bit registers, and there is no MSB register (DS `sde/sde_hw_sspp.c:29-32,737-765`). The SDE IOVA space is 2G..4G (DS `drivers/gpu/drm/msm/msm_smmu.c:305-326`). **[src]**
- XNU on the phone uses only the upper RAM bank, 0x1_00000000 + 0xfcc00000 (NOTES.md:337, docs/SUNFISH.md:16). So every IOSurface page is above 4 GiB. **[obs]**
- Result: with today's untranslated (identity) MDP path, the MDP **cannot fetch any XNU-allocated IOSurface**, whether contiguous or not. Only low-bank memory works, such as the splash region 0x9c000000 + 0x1700000 (DS `arch/arm64/boot/dts/qcom/sdmmagpie.dtsi:614-617`).
- There are two ways forward:
  - **(A) Stage-1 SMMU context for the MDP.** It gives 32-bit IOVA to 40-bit PA and handles scattered pages. Section 2.
  - **(B) Screen surfaces in the splash carve-out.** One 4608 x 2436 surface is 11,225,088 B (0xAB5000 rounded up to 4K). Two of them, 0x9c000000 and 0x9cab5000, fit in 0x1700000 = 24,117,248 B. That allows double-buffered flips with no SMMU work. It only helps if the surface iOS/eagl renders into is created by us from that pool (`sweet_surface_create_physical`, NOTES.md:18), and the Adreno must be able to write it.

## 1. Which pipe ABL uses, and the runtime registers

**Pipes** **[src]**. DS `arch/arm64/boot/dts/qcom/sdmmagpie-sde.dtsi:115-118`; ML `dpu1/catalog/dpu_5_2_sm7150.h:63-104`.

| pipe | MDP offset | CTL_FLUSH bit | stage bits in CTL_LAYER(lm) | scaler |
|---|---|---|---|---|
| VIG0 | 0x4000 | 0 | LAYER bits 0-2 (+EXT bit 0) | QSEED3 v2.4 = QSEED3LITE at +0xa00 |
| VIG1 | 0x6000 | 1 | LAYER bits 3-5 (+EXT bit 2) | same |
| DMA0 | 0x24000 | 11 | LAYER bits 18-20 (+EXT bit 16) | none |
| DMA1 | 0x26000 | 12 | LAYER bits 21-23 (+EXT bit 18) | none |
| DMA2 | 0x28000 | 24 | LAYER_EXT2 bits 0-3 | none |

- Flush bits: DS `sde/sde_hw_ctl.c:72-73`, ML `dpu1/dpu_hw_ctl.c:193-218`.
- Stage bits: DS `sde/sde_hw_ctl.c:164-181`.
- Scaler: DS `sdmmagpie-sde.dtsi:141-142` ("qseedv3lite"), ML `dpu1/dpu_hw_catalog.c:327` (`dpu_vig_sblk_qseed3_2_4`).
- LM0 is at MDP+0x44000, and its flush bit is 6. CTL itself is flush bit 17 (DS `sde/sde_hw_ctl.c:56-58,78-79`).
- CTL n is at MDP+0x1000+0x200n: LAYER(LM0)=+0x00, LAYER_EXT=+0x40, EXT2=+0x70, TOP=+0x14, FLUSH=+0x18, START=+0x1c (DS `sde/sde_hw_ctl.c:20-31`).

**Which pipe ABL uses: UNKNOWN.**
- `shim/disp.c:41-52` scans all 5 pipes. It prints the pipe offset only when the address is not FB_BASE, and no saved log has the `DISP SRC` line (grep of `logs/` found nothing). ABL source is not available, and a web search found nothing definite. **[guess]** It may be DMA0 or VIG0.
- To find out (read-only):
  - log `pipe=` for the matching pipe as well;
  - dump CTL_LAYER(LM0)/EXT/EXT2 of the active CTL and decode them with the table above;
  - also dump SSPP_SRC_FORMAT (0x30), UNPACK (0x34), OP_MODE (0x38), SRC_XY (0x08), OUT_SIZE/XY (0x0c/0x10), and LM0's blend registers.

**SSPP source registers** **[src]**. Offsets from the pipe base, the same in DS `sde/sde_hw_sspp.c:25-37,612-691` and ML `dpu1/dpu_hw_sspp.c`.

| register | offset | value |
|---|---|---|
| SRC_SIZE | 0x00 | h<<16 \| w (the source/crop rect size) |
| SRC_XY | 0x08 | y<<16 \| x (crop origin inside the buffer) |
| OUT_SIZE | 0x0c | h<<16 \| w (destination in the LM) |
| OUT_XY | 0x10 | y<<16 \| x |
| SRC0_ADDR | 0x14 | 32-bit address of plane 0 |
| SRC_YSTRIDE0 | 0x24 | low 16 bits = plane 0 stride (4608 for our surfaces) |
| SRC_FORMAT | 0x30 | |
| UNPACK | 0x34 | |
| SRC_OP_MODE | 0x38 | |
| DECIMATION | 0xb4 | |
| SW_PIX_EXT_C0/C1C2/C3_REQ_PIXELS | 0x108/0x118/0x128 | |

- **Format:** iOS 'BGRA' (bytes B,G,R,A) is the same in memory as a8r8g8b8 / DRM ARGB8888, which ABL already fetches. So SRC_FORMAT and UNPACK can stay as they are. If alpha could be junk, clear SRCC3_EN (FORMAT bit 8) so the pipe acts as XRGB (DS `sde_hw_sspp.c:342-343`). **[guess]**
- **'b3a8' surfaces:** set SRC0_ADDR = surface IOVA + planes[0].offset and stride = planes[0].bytesPerRow, as `present()` already does.
- **Not fit, 1:1 (no scaler, works on a DMA pipe):** SRC_XY = offY<<16 | offX, SRC_SIZE = 2340<<16 | 1080, OUT = 0,0 and 1080x2340. The crop is free, so the CPU copy disappears even without a scaler.

**Latching** **[src]**
- SSPP, LM and CTL registers are double-buffered. Writing CTL_FLUSH marks the blocks pending, and the hardware latches them at the next frame start. For a command-mode panel that is the next CTL_START, or the next autorefresh-triggered transfer.
- Order: write all pipe registers, then CTL_FLUSH = BIT(pipe) | BIT(6) | BIT(17). ML always ORs in BIT(17) with LM flushes (ML `dpu1/dpu_hw_ctl.c:278`).
- CTL_FLUSH reads back the bits that are still pending (DS `sde_hw_ctl.c:310-325`). Use this to know when the new buffer has been taken and the old one can be released.
- CTL_START bit 0 reads 1 while a kickoff is in progress (ML `dpu1/dpu_hw_ctl.c:101-103`).

**Interaction with autorefresh** **[src]** with **[guess]** parts
- DS does **not** flush while autorefresh is running. Before every commit (`sde_encoder_phys_cmd_prepare_commit`, DS `sde/sde_encoder_phys_cmd.c:1589-1635`) it:
  1. disconnects TE: PP_SYNC_CONFIG_VSYNC (PP+0x04) bit 20 off (DS `sde/sde_hw_pingpong.c:390-400`);
  2. writes PP_AUTOREFRESH_CONFIG = 0;
  3. polls PP_LINE_COUNT (PP+0x2c, low 16 bits) until no transfer is in progress (0 or >= vdisplay; DS `sde_encoder_phys_cmd.c:701-735`);
  4. reconnects TE.
- It then programs and flushes, and the "kick" is re-enabling autorefresh, not CTL_START (DS `sde_encoder_phys_cmd.c:1640-1652`).
- **[guess]** A flush alone might latch cleanly at the next autorefresh frame, since only flushed blocks latch. Qualcomm's sequence suggests it is not guaranteed.
- Safest options:
  - (a) follow the DS sequence every frame; or
  - (b) turn autorefresh off once and drive the panel the classic way: program, CTL_FLUSH, CTL_START at TE, wait for CTL_START bit 0 to clear or PP done. `kick()` already writes CTL_START.
- Note: NOTES.md:131 says `sweet_board_is()` reads the registry root, so `cmdModeSetup()` / `kick()` have never run on sunfish. Fix that first.

## 2. SMMU

- **MDP stream** **[src]**: `iommus = <&apps_smmu 0x800 0x440>` (DS `sdmmagpie-sde.dtsi:47`, ML `arch/arm64/boot/dts/qcom/sm7150.dtsi:3957`). Mask 0x440 means SIDs 0x800/0x840/0xc00/0xc40.
  - The secure path is 0x801 mask 0x440 (DS `sdmmagpie-sde.dtsi:244-247`). The rotator is 0x1020/0x1021.
  - Its TBU covers 0x800-0xbff (DS `arch/arm64/boot/dts/qcom/msm-arm-smmu-sdmmagpie.dtsi:203-207`).
  - ACTLR for MDP is `<0x800 0x7ff 0x103>` (same file, :348-350).
  - apps_smmu is at 0x15000000 and has `qcom,skip-init` (:55-61).
- **ABL's state** **[obs]** (NOTES.md:646-656):
  - SMR "0x800 mask 0x440" is valid, put there by the bootloader. SCR0 USFCFG=1, so unmatched streams fault.
  - The hypervisor refuses S2CR bypass: it reads back as fault type.
  - Stage-1 banks you add yourself are accepted. Bank 60 works for WLAN SIDs 0x240/0x241 (NOTES.md:660-664, patch `xnu/patches/0138-*`).
  - S2CRs point at cb0..cb3. Reading context banks "from 0 upward" PANICKED.
  - **The S2CR value of the 0x800 SMR (its type and bank) was printed by wlanprobe=3 but not saved.** Get it first.
- **How Linux takes over** **[src]**:
  - `arm_smmu_handoff_cbs()` (DS `drivers/iommu/arm-smmu.c:4486-4523`) adopts every bootloader SMR whose S2CR is TRANSLATE, together with its bank.
  - SDE attaches to that same bank with DOMAIN_ATTR_EARLY_MAP. With EARLY_MAP, SCTLR.M is left 0 (DS `arm-smmu.c:2010-2012`), so stage 1 is bypassed and the address goes straight to the hypervisor's S2.
  - SDE then maps the splash region 1:1 (`one_to_one_map`, DS `sde/sde_kms.c:907-917`, `_sde_kms_mmu_init` :3335-3410) and only then turns on translation.
  - So **[guess, strong]**: ABL leaves the MDP on a translating S2CR to a bank with stage 1 off, under the hypervisor's identity S2. Stock Linux reprograms that bank (TTBR/TCR/MAIR/SCTLR) under the same hypervisor, so the hypervisor allows writes to it.
- **Plan for (A).** Scattered pages are fine; this is what Linux does.
  1. Read only the 0x800 SMR's S2CR, and only the bank it names.
  2. Pick one of two ways:
     - (i) the Linux way: program that bank. Build page tables first that identity-map 0x9c000000..0x9d6fffff, so the running splash keeps fetching, then set SCTLR.M.
     - (ii) the WLAN way: build a new bank (for example 59) with the same identity range, then rewrite the 0x800 SMR's S2CR to point at it. Read it back, and revert at once if it shows fault type.
  3. Use 4K granule and 32-bit VA: T0SZ=32, start at level 1 (4 entries), then L2, then L3.
  4. Map each screen surface once, at a fixed IOVA slot (for example 16 MiB slots from 0x10000000, away from 0x9c000000), using kvtophys per 4K page. That is about 2741 PTEs, 6 L3 tables. Cache the mapping by surface id.
  5. After changing mappings: CB_S1_TLBIASID (0x610) or TLBIVA (0x600), then TLBSYNC (0x7f0), poll TLBSTATUS (0x7f4) (ML `drivers/iommu/arm/arm-smmu/arm-smmu.h:240-246`).
  6. Copy the rest of the setup from the working WLAN bank code (CBA2R VA64, MAIR0 0x44, SCTLR M|TRE|AFE|ASIDPNE), but use SCTLR CFCFG=0 (terminate, not stall) and leave interrupts off at first.
- **Coherency** **[guess]**: the MDP fetch and SMMU walks are not coherent. Clean page tables (dc cvac). If a surface was written through a cacheable CPU mapping, clean the surface too (about 11 MB of dc cvac per frame, still cheaper than reading and writing 22 MB). GPU-written surfaces with write-combine mappings need nothing.

## 3. Scaling 1125x2436 to 1080x2338

- **Who can scale:** only VIG0 and VIG1 have a scaler: QSEED3LITE, version 0x2004, block at VIG+0xa00, length 0xa0. DMA pipes cannot scale. SSPP line width is 2880 (DS `sdmmagpie-sde.dtsi:144`), so 1125 is fine.
- **Minimal programming** **[src]**, as ML does it: bilinear, no LUT (`lut_flag=0`), DE off (ML `dpu1/dpu_plane.c:423-489`, `dpu1/dpu_hw_util.c:291-389`).
- Scaler registers (VIG+0xa00 + offset):

| register | offset | value |
|---|---|---|
| PHASE_STEP_Y_H | 0x10 | (1<<21)*1125/1080 = **0x215555** |
| PHASE_STEP_Y_V | 0x14 | (1<<21)*2436/2338 = **0x215760** (0x215015 for 2340) |
| PHASE_STEP_UV_H / UV_V | 0x18 / 0x1c | same as Y for RGB |
| PRELOAD | 0x20 | 0x04 \| 0x03<<8 \| 0x04<<16 \| 0x03<<24 = 0x03040304 (QSEED3 defaults H=4, V=3, `dpu_plane.c:53-54`) |
| SRC_SIZE_Y_RGB_A | 0x40 | 2436<<16 \| 1125 |
| SRC_SIZE_UV | 0x44 | same |
| DST_SIZE | 0x48 | 2338<<16 \| 1080 |
| PHASE_INIT_Y_H/Y_V/UV_H/UV_V | 0x90-0x9c | 0 (ML leaves them 0) |
| DIR_FILTER_WEIGHT (3lite) | 0x60 | 0 |
| OP_MODE | 0x04 | see below |

- OP_MODE = BIT0 (enable) | 3<<16 (Y/RGB filter = BIL) | BIT31 (blend_cfg) | BIT14 (not 10-bit) | BIT10 (alpha) | 1<<29 (alpha BIL) = **0xA0034401**. Write it last. Leave out BIT10 and the alpha filter if the layer is XRGB.
- Pipe registers (pipe offsets): SRC_SIZE = 2436<<16|1125, SRC_XY = 0, OUT_SIZE = 2338<<16|1080, OUT_XY = 1<<16 (y=1), DECIMATION 0.
- SW_PIX_EXT_C0/C1C2/C3_REQ_PIXELS = 2436<<16|1125. The LR/TB registers are 0 (ML `dpu1/dpu_hw_sspp.c:373-428`, `dpu_plane.c:492-509`).
- SRC_OP_MODE should keep BIT31 (PE override), as both drivers set it (DS `sde_hw_sspp.c:376`).
- **No LUTs** are needed for bilinear. LUT loading is only for other filters (ML `dpu_hw_util.c:330-335`).
- **Pipe change:** if ABL's pipe is a DMA pipe, scaling means restaging the layer onto VIG0.
  1. Set up VIG0 (format/unpack copied from the DMA pipe, address, rects, scaler).
  2. CTL_LAYER(LM0): clear the DMA stage bits, put the same stage into VIG0's bits 0-2 / EXT bit 0.
  3. Flush BIT(0) | BIT(dma) | BIT(6) | BIT(17).
  4. VIG0's clock control is MDP_TOP+0x2ac bit 0 (DS `sdmmagpie-sde.dtsi:136-138`). **[guess]** It may need forcing on if ABL never clocked VIG0.
- **Destination scaler:** it exists (DS `sdmmagpie-sde.dtsi:78-81,155-157`: DS top at MDSS 0x61000, DS0/1 at +0x800/+0x1000, max input 2048, output 2560). It would need the LM resized to 1125x2436 plus CTL/LM/DSPP reconfiguration. ML has no driver for it. **Not recommended.**
- **Rows 0 and 2339** outside OUT_XY/OUT_SIZE show the LM border color. **[guess]** That is black, from CTL_LAYER BORDER_OUT bit 24 (DS `sde_hw_ctl.c:56`).

## 4. Risks and fallback

What can go wrong:
- **MDP SMMU translation fault** (an unmapped IOVA, or a wrong S2CR):
  - With CFCFG=terminate, the screen shows garbage or black, and the MDP may underrun. **[guess]** A cmd-mode transfer that never finishes stops PP-done; autorefresh just retries.
  - With stall mode, the MDP hangs until resumed. Use terminate.
  - If the hypervisor turns the S2CR into fault type, every fetch faults. Read back immediately and restore.
- **Reading or writing a context bank you do not own** panicked before (NOTES.md:652). Touch only the bank named by the 0x800 S2CR, or your own bank.
- **Freeing or reusing a surface the MDP is still reading:** hold a reference until CTL_FLUSH shows the next flush latched and that frame finished. Delay the "swap complete" to iOS until then.
- **Bad geometry** can cause underruns or a hung pipe: SRC_SIZE past the buffer, stride < width*4, width > 2880, misaligned address, scaler sizes that do not match SRC/OUT sizes. Validate before writing.
- **Flushing while autorefresh is running** may give a torn or half-latched frame **[guess]**. Use the DS sequence (TE off, autorefresh off, wait for line count, TE on) or CTL_START mode.
- **SoC hang:** accesses to unclocked or GDSC-off blocks cause a NoC error and a reset. The MDP is powered as long as splash runs. Never touch DSI/DSC/INTF/PP timing/VBIF/QoS.
- **Bandwidth/QoS:** ABL's danger/safe LUTs and bus votes were sized for a DMA 1:1 fetch. Scaling 1.04x vertically adds about 4% fetch rate. **[guess]** That is fine at the default 300 MHz MDP clock.

Fallback:
- At init, save the active pipe's full register window (0x0-0x1f0, plus scaler 0xa00-0xaa0 for VIG), CTL_LAYER/EXT/EXT2 and PP_AUTOREFRESH.
- Fallback = restore those values (SRC0_ADDR 0x9c000000, ABL's rects, staging), flush, and go back to today's CPU copy into 0x9c000000.
- Gate the feature behind a boot-arg, off by default, and a sysctl to switch at runtime.
- Check after each flush: CTL_FLUSH clears within 2 frames, CB FSR stays 0, and PP_LINE_COUNT moves. If any check fails, fall back automatically.
- Use the hardware path only for a single full-screen opaque layer. Use the copy for anything else (multiple layers, odd formats).

## Suggested order

1. Read-only probe: the active pipe, CTL_LAYER, SSPP format/rects, the 0x800 SMR's S2CR, and that bank's SCTLR/TTBR0 (that bank only).
2. A low-risk test with no SMMU change: flip SRC0_ADDR between 0x9c000000 and 0x9cab5000 (both in the splash carve-out), with SRC_XY crop, using the DS autorefresh sequence or CTL_START. This proves latching.
3. The SMMU stage-1 bank with the splash identity range, then map a real surface.
4. VIG0 plus QSEED3LITE bilinear for fit.
