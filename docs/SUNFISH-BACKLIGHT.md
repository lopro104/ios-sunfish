# Pixel 4a (sunfish) display brightness: research notes

All paths are relative to `~/EvolutionX/kernel/google/msm-4.14` unless they say otherwise.
Abbreviations: `PANEL` = `arch/arm64/boot/dts/google/dsi-panel-sofef01-1080p-cmd.dtsi`,
`DSI` = `drivers/gpu/drm/msm/dsi-staging`.
Nothing was run on the phone. Points marked **[uncertain]** are inferences, not read from source.

## 1. Brightness is set by DCS 0x51, 10-bit, big-endian. There is no PWM or WLED.

- `PANEL:52` `qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs"`, `PANEL:53-54` min 4, max 1023 ("10 bit").
  `sm7150-sunfish-display.dtsi:63-65` disables `pm6150l_wled`. Brightness comes from the panel only.
- The panel node has `compatible = "google,dsi_binned_lp"` (`PANEL:15`). That selects
  `dsi_panel_binned_lp_register` (`DSI/dsi_backlight.c:1063-1067`), which sets
  `update_bl = dsi_panel_binned_bl_update` (`:1052`). Outside AOD/LP mode it calls
  `dsi_backlight_update_dcs` (`:963-967`).
- `dsi_backlight_update_dcs` (`DSI/dsi_backlight.c:85-109`) uses 2 parameters because max 1023 > 0xFF (`:104`), then calls
  `mipi_dsi_dcs_set_display_brightness` (`drivers/gpu/drm/drm_mipi_dsi.c:1074-1097`). That sends
  payload `[0x51, lvl>>8, lvl&0xFF]` (high byte first). With 3 bytes it goes out as a **DCS long write, data type 0x39**
  (`drm_mipi_dsi.c` `mipi_dsi_dcs_write_buffer`, the `default:` case after case 2, around line 689).
  - Example: full normal brightness is `39: 51 03 FF`. Minimum is `51 00 04`.
- UI to panel mapping: `dsi_backlight_calculate_normal` (`DSI/dsi_backlight.c:130-157`). There is no LUT in the dtsi,
  so it linearly interpolates user 1..255 (`brightness_max_level` defaults to 255, `:1629-1636`) onto 4..1023.
  Per the `PANEL:214-221` comment, normal mode covers about 2-450 nits.
- Register 0x53 (WRITE_CTRL_DISPLAY) must be 0x20 (BCTRL on). The on-sequence sends `15: 53 20` (`PANEL:89-90`).
  ABL runs the same on-sequence before handing over, **[uncertain]** that ABL uses exactly the same table, so in
  practice only 0x51 is needed. Other 0x53 values used:
  - `53 28`: normal, with dimming (smooth ramp over 32 frames), `PANEL:170,200`. `53 20` stops dimming, `PANEL:177,207`.
  - `53 E8` / `53 E0`: **HBM on**, with / without dimming (`PANEL:233,240`). In HBM, 0x51 is re-scaled to 0..0x194
    for about 470-700 nits (`PANEL:213-227`). Linux enables HBM only when userspace writes `hbm_mode`
    (`DSI/dsi_backlight.c:415-418, 605-624`). It is not part of the normal slider.
  - `53 22` / `53 23`: HLPM (AOD) at 50 nits / 10 nits (`PANEL:114,145,157`). While HLPM is active, Linux sends no 0x51
    (`DSI/dsi_backlight.c:963-965`).
  - Samsung-specific bit reading (BCTRL = bit 5, dimming = bit 3, HBM = bits 7:6, HLPM = bits 1:0) is
    **[uncertain]**. It is inferred from the comments next to these values.
- Recommended minimal driver: write only `51 HH LL`, clamping lvl to 4..1023. Do not send 0. Optionally send
  `53 28` first for smooth dimming, but then `53 20` must follow about 32 frames later. Simplest: leave 0x53 alone.
- Commands go out in **HS mode**. The `mipi_device` never sets `MIPI_DSI_MODE_LPM` (no `mode_flags` assignment in
  `DSI/dsi_panel.c`), so `use_lpm` is false (`drm_mipi_dsi.c:375-377`; `DSI/dsi_ctrl.c:1222-1223`). The panel
  on/off tables use LP (`PANEL:124-125`), but brightness does not.

## 2. How Linux sends the command (DSI ctrl v2.3, DSI0 at 0x0AE94000)

- `arch/arm64/boot/dts/qcom/sdmmagpie-sde.dtsi:404-410`: `mdss_dsi0` @ `0xae94000`, size 0x400,
  `compatible "qcom,dsi-ctrl-hw-v2.3"`. The PHY is at 0xae94400 (`:502-506`). Sunfish uses ctrl 0 and phy 0
  (`sm7150-sunfish-display.dtsi:39-47`).
- Call path: `mipi_dsi_device_transfer` always adds `MIPI_DSI_MSG_LASTCOMMAND` (`drm_mipi_dsi.c:373-378`), then
  `dsi_host_transfer` (`DSI/dsi_display.c:2790-2876`). That function turns clocks on, enables the cmd engine, and
  calls `dsi_ctrl_cmd_transfer(..., DSI_CTRL_CMD_FETCH_MEMORY)` (`:2849-2855`). Then `dsi_message_tx`
  (`DSI/dsi_ctrl.c:1144-1376`).
  - Linux uses the **memory-DMA path**: the packet sits in a GEM buffer, and `DSI_DMA_CMD_OFFSET` = its SMMU IOVA
    (`DSI/dsi_ctrl.c:1205-1232`; `dsi_ctrl_hw_cmn.c:660-702`).
  - There is also a **FIFO path** (`DSI_CTRL_CMD_FIFO_STORE`) that pushes the packet through
    `DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL` registers and needs no memory or SMMU. Linux uses it during secure
    sessions (`DSI/dsi_ctrl.c:1081-1086`; hw `dsi_ctrl_hw_cmn.c:716-772`). It is wired for v2.3
    (`DSI/dsi_catalog.c:39`, `:112-113`). It holds at most 64 bytes (`DSI/dsi_ctrl.h:60`). **This is the one to use
    from XNU.**
- Packet format in the Qualcomm DMA buffer: Qualcomm's `mipi_dsi_create_packet` changes the byte order
  (`drm_mipi_dsi.c:474-495`). Header bytes are `[WC_lo or param0, WC_hi or param1, VC<<6 | DT, flags]`.
  `dsi_ctrl_copy_and_pad_cmd` (`DSI/dsi_ctrl.c:983-1011`) pads the payload to 4 bytes with 0xFF and sets flag
  BIT(6) for a long packet. `dsi_message_tx` sets BIT(7) for the last command (`DSI/dsi_ctrl.c:1202-1203`). The
  hardware computes the ECC.
  - `51 HH LL` (long, 3 bytes): word0 = `0xC0390003`, word1 = `0xFF000051 | LL<<16 | HH<<8`, length = 8.
  - Short 1-param write such as `53 20`: word0 = `0x80152053`, length = 4. The FIFO path then appends one zero
    dword, because an odd dword count needs padding (`dsi_ctrl_hw_cmn.c:739-740`).
- Registers (offsets from 0x0AE94000, `DSI/dsi_ctrl_reg.h`):

  | off | name | use |
  |---|---|---|
  | 0x004 | DSI_CTRL | bit0 DSI_EN, bit1 VIDEO_MODE_EN, bit2 CMD_MODE_EN (`dsi_ctrl_hw_cmn.c:583-650`). Both bit0 and bit2 should already be 1 from ABL. |
  | 0x008 | DSI_STATUS | bit2 = CMD_MODE_MDP_BUSY (`dsi_ctrl_hw_cmn.c:1576-1588`) |
  | 0x03C | DSI_COMMAND_MODE_DMA_CTRL | bit31 broadcast, bit30 master, bit28 embedded mode (set), bit26 LP (clear for HS), bit24 / bit29 cleared on the memory path (`dsi_ctrl_hw_cmn.c:666-685, 752-768`) |
  | 0x048 / 0x04C | DMA_CMD_OFFSET / DMA_CMD_LENGTH | memory path address; length (bytes) on both paths |
  | 0x084 | DSI_TRIG_CTRL | DMA trigger = SW (`PANEL:39` `trigger_sw`; map `dsi_ctrl_hw_cmn.c:78-88`). bit31 = TE on external pin. **Leave untouched.** |
  | 0x090 | DSI_CMD_MODE_DMA_SW_TRIGGER | write 1 to send (`dsi_ctrl_hw_cmn.c:701, 768, 792-796`) |
  | 0x110 | DSI_INT_CTRL | bit0 CMD_MODE_DMA_DONE status (write 1 to clear), bit1 its enable. bit24 error status (do not write 1 to it), bit25 error mask (`dsi_ctrl_hw_cmn.c:898-1010`) |
  | 0x15C | DSI_TEST_PATTERN_GEN_CTRL | FIFO mode: `BIT(1)\|BIT(2)\|(3<<16)` = 0x00030006 (`dsi_ctrl_hw_cmn.c:728-729`) |
  | 0x17C | TPG_CMD_DMA_INIT_VAL | write the packet dwords one at a time (`:735-740`) |
  | 0x1EC | DSI_TPG_DMA_FIFO_RESET | 1, then about 1 µs later 0, after every transfer (`:774-781`) |

- **Minimal sequence** (FIFO path, mirroring `dsi_ctrl_hw_cmn_kickoff_fifo_command` and the `dsi_message_tx`
  wait/cleanup at `DSI/dsi_ctrl.c:1302-1366`):
  1. Map 0x0AE94000 (0x400 bytes covers all offsets above). Check that `DSI_CTRL & 0x5 == 0x5`. If not, the
     display is not in cmd mode or is off, so do nothing.
  2. Clear a stale DMA_DONE: `INT_CTRL = (INT_CTRL & 0xA222AA02) | BIT(0)`. The mask keeps only the odd enable bits
     and bit25, so other status bits are not cleared by accident. This is safer than Linux's plain
     read-modify-write.
  3. `W(0x15C, 0x00030006)`, then `W(0x17C, 0xC0390003)`, then `W(0x17C, 0xFF000051 | LL<<16 | HH<<8)`.
  4. `v = R(0x03C); v &= ~(BIT31|BIT30|BIT26); v |= BIT28; W(0x03C, v)`.
  5. `W(0x04C, 8)`, then `dsb`, then `W(0x090, 1)`.
  6. Poll `INT_CTRL` bit0 with about 50 ms timeout (Linux uses 200 ms, `DSI/dsi_ctrl.c:38`), then clear it as in
     step 2.
  7. `W(0x15C, 0)`, then `W(0x1EC, 1)`, then `udelay(1)`, then `W(0x1EC, 0)`.
  - Do not enable the DMA_DONE interrupt (bit1) unless an IRQ handler exists. Polling bit0 works without it: Linux
    itself falls back to reading the status bit when the IRQ is not seen (`DSI/dsi_ctrl.c:1333-1346`).
    **[uncertain]** that the status bit latches when the enable bit is 0. It probably does: Linux enables the IRQ
    before kickoff, so this fallback does not prove it.
- Clocks: ABL's continuous splash and our autorefresh keep the DSI byte/pixel/esc clocks running (frames arrive on
  every TE), so no clock work is needed. **[uncertain]** whether `DSI_CLK_CTRL` (0x11C) dynamic clock gating
  matters. Linux sets `DSI_CTRL_DYNAMIC_FORCE_ON` only when toggling DSI_EN (`dsi_ctrl_hw_cmn.c:603-622`), not for
  command TX.

## 3. Risks

- **Autorefresh / MDP stream running at the same time.** Linux `dsi_host_transfer` and `dsi_message_tx` do not
  pause the MDP stream or wait for MDP idle in cmd mode (`DSI/dsi_display.c:2790-2876`). Android sends brightness
  while frames are being kicked every TE. `dsi_ctrl_wait_for_video_done` (`DSI/dsi_ctrl.c:1303`) only matters in
  video mode, and `schedule_dma_cmd` is video-only (`:1268-1274`). Linux's autorefresh-disable path
  (`drivers/gpu/drm/msm/sde/sde_encoder_phys_cmd.c:1589-1634`: disable TE, clear autorefresh, poll
  `PP_LINE_COUNT` until not mid-frame, re-enable TE) runs only before a new frame commit, not for DCS commands.
  - Conclusion **[uncertain, inferred]**: the DSI controller arbitrates DMA against the MDP stream itself, so a
    SW-triggered DMA packet is sent between MDP packets or frames. `max_cmd_packets_interleave` is hard-coded to 0
    (`DSI/dsi_panel.c:1747-1748`; field `DSI_COMMAND_MODE_MDP_CTRL[23:20]`, `dsi_ctrl_hw_cmn.c:557`).
  - Autorefresh produces the same traffic pattern as Android's per-TE kickoffs, so it should be safe without
    pausing.
  - If glitches appear, one option is to wait until the pingpong is between frames before triggering. Use
    `PP_LINE_COUNT` (PP+0x02C, low 16 bits) equal to 0 or at least 2340, as in
    `sde_encoder_phys_cmd.c:701-737`, or `DSI_STATUS` bit2 = 0. The heavier option is the full autorefresh
    disable/re-enable sequence above.
- **Do not reprogram** DSI_TRIG_CTRL, DSI_COMMAND_MODE_MDP_CTRL / DCS_CMD_CTRL, the TE pin config, or the PHY. They
  hold ABL's setup.
- **Memory DMA path** requires a DSI-visible address through the MDSS SMMU (`cmd_buffer_iova`). The FIFO path avoids
  this. After a non-embedded-mode DMA, Linux soft-resets the controller (`DSI/dsi_ctrl.c:1356-1364`, "otherwise
  smmu write faults"). Embedded mode or FIFO does not need this.
- **Never send 0x51 = 0.** The minimum is 4 (`PANEL:53`). Avoid HBM (0x53 E0/E8) unless you implement the 0..0x194
  remap. In HBM, 0x3FF is out of range (`PANEL:226-227`).
- **Dimming:** if you write `53 28`, the panel ramps every change over about 32 frames. Linux sends `53 20`
  afterwards (`dimming_workq`, `DSI/dsi_backlight.c:160+`). Sending raw 0x51 with 0x53 = 0x20 makes the change
  instant.
- The panel has TE check / ESD enabled (`PANEL:45`), but that is a Linux-side reader. It does not apply to us.
- Only one master may drive the DSI DMA engine. Nothing else in our stack (shim or XNU patch 0084) touches DSI
  registers today: `~/ios-sunfish/shim/disp.c` and `xnu/patches/0084-sunfish-rtc-display-kick.patch` touch only
  MDP CTL and PP. Serialize brightness writes with a lock.

## Board note

The Redmi Note 10 Pro (`sweet`) panel is different (video mode). Gate any of this on ADT compatible
`google,sunfish`, per `~/ios-sunfish/AGENTS.md`.
