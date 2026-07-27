# ESP32-P4 Hardware Options — SeedSigner MicroPython Port

**Status:** working notes, current as of July 2026
**Scope:** off-the-shelf ESP32-P4 boards suitable as build targets for the MicroPython port, evaluated for second-sourcing and for eventual replacement by an open-source PCB. The document ends (§10–§11) in a concrete **shopping list** and **test matrix** — the boards and parts to actually buy, and what each purchase is meant to prove.

---

## 1. Project constraints driving this evaluation

| Constraint | Implication |
|---|---|
| No single-vendor lock-in beyond the ESP32-P4 itself | Need ≥2 viable vendors with meaningfully different hardware |
| Airgapped signing device | Radio-free preferred; radio-present acceptable only if compiled out of a reproducible build |
| QR capture (PSBTs, seeds, animated QRs) | Camera required, **rear-facing** (aimed away from the screen), with usable close focus |
| Responsive UI | MIPI-DSI only — SPI/QSPI panels bypass the P4 display engine and are a significant usability regression |
| Low-friction sourcing | Ideally one product page, camera included or explicitly named |
| Continued Raspberry Pi Zero support | Component overlap with the Pi build line is a bonus, not a side effect |
| Future open-source PCB | Boards with published schematics have extra value as reference designs |

### The unavoidable trade-off

**No vendor currently sells a radio-free ESP32-P4 board with an integrated touchscreen.** Every all-in-one panel carries a soldered ESP32-C6, because the P4 has no radio of its own and vendors add one to make the product marketable. This is now confirmed for all three all-in-one candidates below (Waveshare 4.3, Elecrow, M5Stack Tab5 — each has a soldered C6). Radio-free options are all bring-your-own-panel (Olimex). Any board list has to pick a side of this, or carry both — which is what this document does: an all-in-one primary + DIY candidates, plus a radio-free second source.

### The other recurring trade-off: camera orientation

Most all-in-one P4 panels put the camera on the **same face as the screen** (front-facing, for video calls), which is the wrong geometry for aiming at a QR while reading the screen. The good news from this round of research: on every candidate here the camera is a **discrete module on a flex cable plugged into a CSI connector**, not a soldered-down bezel part. That makes "flip it to face rearward" a mechanical/cabling problem, not a board-respin problem — see §6 and the per-board notes.

### Board roles at a glance

| Board | Role | Radio | Display |
|---|---|---|---|
| **Waveshare P4-43** (WIFI6-Touch-LCD-4.3) | **Primary release target** | C6 (compiled out) | integrated 4.3" 480×800 DSI + touch |
| **Elecrow CrowPanel Advance P4 5"** | **Non-Waveshare all-in-one — recommended second source (daily-driver)** | C6 soldered (compiled out) | integrated 5" DSI + GT911 |
| **Guition JC4880P443** | Budget non-Waveshare all-in-one — closest twin of the release target; **camera reroute confirmed** (owned) | C6 soldered (compiled out) | integrated 4.3" 480×800 DSI (ST7701S) + GT911 |
| **M5Stack Tab5** | Camera-reroute experiment (buy one to assess) | C6 soldered | integrated 5" 1280×720 DSI + GT911 |
| **Olimex ESP32-P4-PC / DevKit** | Radio-free inspection + open-KiCad **PCB reference** (not a daily-driver) | **none (radio-free)** | bring-your-own (commodity ST7701S+GT911) |

The logic: one primary (Waveshare); **two independent all-in-ones** to prove no single-vendor lock-in — Elecrow (cleaner public docs, Western distribution) and Guition (a near-exact, cheaper twin of the release target); one cheap camera experiment (Tab5); and one radio-free open-hardware board whose real value is inspection + a custom-PCB starting point (Olimex) — *not* a second daily-driver, because making it one would just re-introduce a Waveshare-origin panel dependency (§7).

---

## 2. Primary build target (current)

### Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

| | |
|---|---|
| Display | 4.3" 480×800 IPS, capacitive touch, **portrait by default** |
| Camera | MIPI-CSI interface; OV5647 5MP available bundled or separately |
| Radio | ESP32-C6 present — compiled out of custom firmware |
| Other | 3.7V MX1.25 LiPo header, microSD, 40-pin GPIO (RPi HAT-compatible), audio codec |
| Sourcing | Waveshare direct + Amazon; **two retail SKUs, with and without camera** |

**Suitability: primary.** Portrait orientation, battery header, and an OV5647 option make this close to purpose-built for a handheld signer. OV5647 also carries sensor continuity from the Pi Zero builds, and its CSI uses the RPi-standard **15-pin 1.0mm** flex, which opens up the long-cable / close-focus module ecosystem in §6.

**Pros**
- Single SKU including camera
- Battery header and portrait panel suit the handheld form factor
- Camera sensor (OV5647) shared with existing Pi Zero builds; RPi-standard CSI cable

**Cons**
- ESP32-C6 physically present
- Single vendor for both board and panel
- 480×800 is modest, though well-matched to P4 framebuffer bandwidth

**Action item:** pin project docs to the *bundled-camera* listing. The Waveshare store page treats OV5647 as optional and there is a camera-less retail SKU that buyers can land on by mistake.

#### On the ESP32-C6 compromise

This is a defensible position, not just a concession to availability:

- The C6 is a **separate die behind SDIO/UART**. It cannot self-initialize; the P4 must bring it up. This differs materially from a Pi Zero W, where the radio is on the SoC.
- SeedSigner's trust model already rests on **reproducible builds and signed releases** rather than hardware inspection. "No C6 driver in the binary" is verifiable through the mechanism users already exercise.
- Physical absence remains strictly stronger. This is what the custom PCB (and the radio-free Olimex second source) resolves.

---

## 3. Active DIY candidates (all-in-one, camera needs repositioning)

These are the boards worth running and modifying by hand — all strong on display and toolchain, all with the camera on the wrong (front) face and needing it flipped rearward (already confirmed working on the Guition — §3c). Elecrow (§3a) and Guition (§3c) are the two non-Waveshare all-in-one contenders; the Tab5 (§3b) is a cheap experiment to gauge how practical the camera flip is. The point is to learn how each behaves in the hand.

### 3a. Elecrow CrowPanel Advance P4 — **the non-Waveshare all-in-one (recommended second source)**

| | |
|---|---|
| Lineup | 5" (800×480), 7" / 9" / 10.1" (all 1024×600) — **all IPS, all MIPI-DSI, all landscape**. Smallest P4 variant is **5"**; there is no ≤4.3" or native-portrait P4 model (the small 2.4–4.3" CrowPanels are the separate **ESP32-S3** line). |
| Touch | Capacitive **GT911** (I²C), 5-point — same controller family as the P4-43 release target |
| Camera | **Optional 2MP MIPI-CSI add-on** (≈ +$8), a discrete module on a flex into the board's `CSI-CAM` connector — **not bezel-mounted**, so physically repositionable |
| Radio | **ESP32-C6-MINI-1 soldered** to the mainboard (UART link, factory-locked firmware) |
| Memory | 16MB flash + 32MB PSRAM (ESP32-P4NRW32) |
| Design files | **Published on GitHub** (org `Elecrow-RD`), Autodesk **Eagle** format (.sch/.brd) + 3D models + factory firmware |
| Price | 5" ≈ $42.90 · 7" $45.90 / $53.90 w-camera · 9" ≈ $50.90 · 10.1" ≈ $53.90 (Elecrow direct, July 2026) |

**This is the second source for a *usable* device.** Elecrow is a wholly independent vendor from Waveshare that integrates its own DSI panel + GT911 touch + camera — so it sidesteps the handheld touch-DSI-panel monoculture (§4/§7) entirely: nothing to source, ~$50 all-in, cheaper than Olimex + a bought panel. **Why it works technically:** MIPI-DSI panels across the board (no SPI regression), GT911 touch matching the release target, published schematics, and a camera that is a loose flex module rather than a soldered bezel part. The camera faces the wrong way out of the box, but because it's a discrete module it can be **pinned/folded back to face rearward** — the mod you already have in mind.

**Confirmed corrections to earlier notes**
- The "swappable wireless module slot" is **not** the C6 — the C6 is soldered. The slot is a *separate* 2×7-pin header for optional add-on radios sold under SKU `DAC0010` (ESP32-H2 / SX1262 LoRa / nRF24 / WiFi-HaLow). For an airgapped signer, leave that slot empty and leave the soldered C6 un-driven.
- Elecrow **does** sell a matched camera (the +$8 add-on), resolving that open question — but see the gaps below.

**Open gaps (resolvable, mostly from the GitHub Eagle schematic)**
- **Camera sensor model is unpublished.** Elecrow only says "2MP MIPI-CSI." It is *not* OV5647 (that's 5MP); a 2MP P4 sensor is most commonly **SC2336**, which is already in `esp_cam_sensor` (§6) — plausible but unverified. This means the Elecrow camera does **not** share the OV5647 sensor path with the Waveshare/Pi builds.
- **Camera cable spec is unpublished** — pin count, pitch, length, and whether it's the RPi 15-pin 1.0mm flex. The P4 CSI is **not electrically guaranteed pin-compatible with an RPi camera cable** even where a connector physically fits, so a generic long-flex swap is not safe to assume. Resolve by reading the `CSI-CAM` footprint in the GitHub Eagle schematic before buying any longer cable.
- Camera **default orientation** and how much flex slack exists for repositioning — a physical-inspection item once a unit is in hand.

**Form-factor note:** 5" landscape (800×480, rotate to 480×800 in LVGL) is the smallest option and is larger than the 4.3" release target — acceptable for a bench/dev target, worth weighing for a pocket handheld.

### 3b. M5Stack Tab5 — DIY camera-reroute candidate (buy one to assess)

| | |
|---|---|
| Display | 5" 1280×720 MIPI-DSI IPS, **GT911** capacitive touch |
| Camera | **SC2356 2MP, front-facing**, on a **removable 24-pin FPC** (connector J4, 2-lane CSI) |
| Radio | ESP32-C6 (WiFi6/BT/802.15.4) soldered |
| Memory | 16MB flash + 32MB PSRAM |
| Design files | **Schematics + block diagram published** (M5Stack docs, 6-page PDF) |
| Price | **$55** (no battery) / **$60** (with NP-F550 battery), M5Stack direct |

**Why buy one:** broadest P4 toolchain support of any board (UIFlow / Arduino / ESP-IDF / PlatformIO-adjacent), a sharp 1280×720 panel, published schematics, and — critically — the camera is a **plug-in 24-pin FPC module (J4), not soldered.** The teardown and schematic both confirm it unplugs. The purchase is a practical experiment: *how hard is it to reroute this camera to rear-facing?*

**Feasibility verdict: MODERATE, and it's mainly a mechanical job.** Three obstacles, ranked:
1. **Electrical disconnect — easy.** Camera is on connector J4, no desoldering.
2. **Cable — likely a non-issue.** The stock flex already routes from J4 up to the front camera position at the top of the board, so it carries real length and slack; folding/rerouting it to a rear-facing position probably needs **no new cable at all**. (This drops the earlier signal-integrity concern about extending the CSI link — that only applied if an extender were spliced in, which reusing the stock flex avoids.) Verify actual slack once a unit is in hand. Note the flex is a **24-pin FPC, not the RPi 15-pin format** (pitch likely 0.5mm — the schematic states 24-pin only; measure to confirm), so OV5647/IMX219 modules do not drop in. (Encouraging precedent: the Guition JC4880P443, §3c, confirms this fold-the-stock-flex reroute works by hand on a comparable front-camera P4 board — though the Tab5's enclosure and lack of a rear window differ.)
3. **Rear mounting — the dominant task (moderate-to-hard, mechanical).** There is **no rear optical window**; the camera needs an opening cut in the back shell and a fabricated bracket. With the cable a non-issue, this is essentially the whole job.

**The camera-swap alternative was investigated and doesn't pan out off-the-shelf.** The tempting shortcut — plug a *replacement* rear-facing module into J4, leave the front one disconnected — founders on the fact that **MIPI-CSI camera FPC pinouts are vendor-specific, not standardized.** J4 is only a generic "FPC_24P" footprint in M5's schematic: no mating-connector part number published, pitch not even stated. M5Stack doesn't sell the Tab5 camera as a spare, and no catalog module publishes a pinout that matches J4. The closest cousin — the **Seeed reTerminal D1001** (same SC2356, same 2-lane CSI) — has an *undocumented* FPC pinout and also isn't sold separately (you'd harvest one from a whole ~$85 device on spec). A true drop-in doesn't exist today; the swap path would mean reverse-engineering J4's pin map and commissioning a **custom/adapter flex**. **Conclusion: reuse the stock flex (obstacle 2 above) — the camera already reaches its mount, so this is a mechanical remount, not a sourcing problem.**

**Not a shortcut:** M5's Grove/UART "QR scanner" units are on-module *decoders* (they return decoded strings for static 1D/2D codes) and will not handle SeedSigner-class dense/animated UR/BBQr, which need raw camera frames. They do not replace the CSI camera.

### 3c. Guition JC4880P443(C) — budget non-Waveshare twin of the release target

| | |
|---|---|
| Display | **4.3" 480×800 IPS, true MIPI-DSI, driver ST7701S** — same panel *class* as the Waveshare P4-43 release target (not QSPI/RGB) |
| Touch | **GT911** capacitive (I²C) |
| Camera | 2MP MIPI-CSI, **front-facing**, on a removable **board-specific** FPC (stamped `ZB2455 JC4880P43-V2.0`); sensor die unconfirmed — likely **SC2336** (Guition's other P4 boards use it @ I²C 0x36), *not* the Tab5's SC2356 or OV5647 |
| Radio | **ESP32-C6-MINI-1U soldered** (SDIO coprocessor) |
| Memory / other | ESP32-P4NRW32 (32MB in-package PSRAM) + 16MB flash, microSD, **ES8311 audio**, MX1.25 Li-ion battery connector, ~117×69mm portrait slab |
| Design files | Not on the AliExpress listing, but the **vendor publishes a full doc package** — Guition download center `pan.jczn1688.com` → `JC4880P443C_I_W.zip` (pinout + schematics + ST7701/GT911/ES8311 drivers + Arduino examples). Plus **merged ESPHome support** (`mipi_dsi` model `JC4880P443`) and community LVGL / Home-Assistant configs. |
| Price | ≈ $32 base (~$29–40 with camera/shell) — roughly half the Waveshare P4-43 |

**Why it matters:** the **closest hardware twin of the release target** — identical 4.3" 480×800 ST7701S-over-MIPI-DSI panel class + GT911 touch — from a *different, independent vendor* (Guition / Shenzhen JCZN) at about half the price, with camera, battery header, audio, and microSD. It's the strongest single data point for the anti-lock-in claim: the *integrated* 4.3" 480×800 P4 board exists from at least three unrelated vendors (Waveshare, Guition, and — at 5" — Elecrow), even though the *bare-panel-module* market is concentrated (§7).

**Corrections to the initial read:** it *does* have published schematics (on the vendor download center, not the listing), and it's genuine **MIPI-DSI, not QSPI/RGB**. (An AI-generated AliExpress "wiki" article claiming SPI/RGB + GC9A01 is wrong; vendor, ESPHome, and CNX sources agree on ST7701S/DSI.)

**Catches**
- **Camera faced the wrong way (front) — but the reroute is confirmed working.** Opening the case and **folding the stock camera flex back** reorients the module to face rearward (out the back, away from the screen) — no new cable, no desoldering. This is the same fold-the-existing-flex approach proposed for the Tab5 (§3b), and on the Guition it's been done by hand and verified. (Swapping in a *different* camera remains out — the flex/pinout is bespoke — but that's moot now that the reroute works.)
- **Sensor die unconfirmed.** "ZB2455" is an internal code with no public footprint; SC2336 is the best inference — confirm by I²C probe at 0x36 or the vendor schematic. If SC2336, the Espressif EV-board's SC2336 + `esp_cam_sensor` stack is the reference; it does *not* share the OV5647 path.
- **Docs live on a Chinese network drive** (`pan.jczn1688.com`), not a clean public repo like Elecrow's Eagle files or Olimex's KiCad — less convenient/durable, though ESPHome's merged support mitigates it.

**Build-readiness (high):** a near-twin of the release target — the ST7701 display driver, GT911 touch, and board-def framework already exist in-repo, so it's a forked `board_config.h`, not a port. The confirmed pinmap (from ESPHome) and all candidate schematics are collected under [board-schematics/](board-schematics/) (see the [Guition vendor-docs note](board-schematics/guition-jc4880p443/VENDOR-DOCS-NOTE.md)). Display + touch bring-up needs only the forked config; camera is phase 2 pending the vendor schematic (manual download) or an on-device I²C probe (0x36 → likely SC2336).

**Role:** a genuine **budget non-Waveshare all-in-one** and the best hardware twin of the release target for cross-vendor validation. **With the camera reroute already proven by hand, it has pulled ahead of Elecrow on the *practical* axis** — exact release-target form factor, cheapest, and the one board where the front-camera problem is already solved. Against Elecrow (§3a): Guition wins on form-factor match (portrait 4.3" 480×800 vs Elecrow's landscape 5" 800×480), price, and camera-solved status; Elecrow's remaining edge is **documentation quality** (public GitHub Eagle files vs a vendor network-drive zip) and Western distribution — which matter for a reproducible-build, security-reviewed project. Evaluate both hands-on; you already own the Guition.

---

## 4. Radio-free reference board — Olimex (not a daily-driver)

### Olimex ESP32-P4-PC / ESP32-P4-DevKit — **radio-free inspection + open-KiCad PCB reference**

**Role: radio-free inspection board + open-hardware PCB reference — not the daily-driver second source** (that's Elecrow, §3a). Olimex earns its place on three things Waveshare and Elecrow can't offer: it's **radio-free by silicon** (no WiFi/BT die at all — inspectable-clean for the airgap claim), its **entire design is published as KiCad** (forkable as the starting point for the custom PCB), and it's a **long-lived, Western-distributed** open-hardware vendor. What it is *not* is a clean usable-device target: it's bring-your-own-panel, and the handheld touch-DSI-panel supply is a Waveshare-origin monoculture (§7), so making Olimex a daily-driver would just reintroduce the Waveshare dependency that Elecrow avoids. Buy Olimex to **inspect, bring up, and harvest the schematic** — not to ship a device on. (It does sell a matching raw DSI panel + HDMI adapter + OV5647 camera, so you *can* light it up end-to-end for evaluation; details below.)

| | ESP32-P4-PC | ESP32-P4-DevKit |
|---|---|---|
| Price | €24.95 | €16.00 |
| Display path | **Onboard HDMI** (integrated converter) + MIPI-DSI connector | MIPI-DSI connector (HDMI only via add-on adapter) |
| Camera | MIPI-CSI connector | MIPI-CSI connector |
| Radio | **None** (Ethernet only; wireless addable via UEXT if ever wanted) | **None** |
| Other | 16MB flash, 32MB PSRAM, 4× USB2.0 host, microSD, audio, UEXT, 20-pin GPIO | USB-C JTAG, Ethernet (PoE option), microSD, UEXT, all GPIOs |
| Design files | **KiCad sources + PDF schematics published** | Published |

**Radio-free by silicon** — the P4 has no built-in WiFi/BT, so these boards are inspectable-clean for the airgap requirement. This is the dual-purpose pick: a manufactured, radio-free P4 schematic with DSI/CSI/power/storage already routed is a better starting point for the custom PCB than the Espressif reference design.

**Display options — and the touch gap (the important correction):**

- **Olimex `MIPI-LCD2.8-640×480`** — €24.95, 2.8" IPS 640×480, driver IC **ST7701S**, single-lane DSI, 15-pin 1.0mm RPi-layout FPC; plugs into both boards, no adapter. **Confirmed display-only — no touch of any kind, and it's the *only* DSI panel Olimex makes.** Its value is narrow but real: a *guaranteed-to-work* panel (Olimex ships LVGL + DSI + camera demo code for it) for **DSI-engine bring-up**. It is **not** a usable UI display for a touch-driven signer.
- **Usable touch display → an ST7701S + GT911 480×800 DSI panel (the *ICs*, not a vendor).** ST7701S (Sitronix) and GT911 (Goodix) are commodity parts with **first-party `esp_lcd` drivers** (`espressif/esp_lcd_st7701` does P4 MIPI-DSI; `espressif/esp_lcd_touch_gt911`), so any such panel is bring-up-able regardless of who sells it — and both match the P4-43 release target. **Sourcing reality (the important catch):** at the ~4" 480×800 native-DSI / RPi-15-pin form factor, the *finished-panel* market is effectively **one Waveshare-origin reference design**, cloned by a few storefronts. The ICs are multi-sourced; the finished small panel, at this size/interface, is not. Three honest paths:
    - **Cheapest handheld-size drop-in:** **Spotpear 4" 480×800** (~$49) — a non-Waveshare *storefront*, but the same OEM design. Fine to light up the port; weak design-independence.
    - **Strongest vendor-neutral proof:** the Espressif reference **EK79007 (1024×600) + GT911** panel (~$75, e.g. PiShop) — a *different* display controller with its own first-party driver, proving the GT911 + P4-DSI stack isn't tied to ST7701S *or* Waveshare. Downside: 7", bench not handheld.
    - **Real independence (the endgame):** a bare ST7701S module from an independent maker (Raystar, DisplayModule, BuyDisplay) + a custom 15-pin DSI FPC — exactly what the custom PCB resolves.
  - **Gotcha:** many "RPi DSI" 800×480 panels use a Toshiba **TC358762 DSI→DPI bridge** (like the official RPi 7"), which does **not** reuse the ST7701S driver — verify a panel is *native* ST7701S before buying.
- **The touch-wiring catch (confirmed from Olimex's published KiCad schematic).** On the RPi 15-pin DSI standard, touch I²C rides the *same* FPC (pin 11 = SCL0, pin 12 = SDA0) — but **Olimex did not route those pins**: its DSI connector carries display lanes only (the connector symbol has no SCL/SDA pin), because its own panel has no touch. The board *does* expose an I²C bus — on the DevKit the schematic confirms ESP32-P4 **GPIO7 = SDA, GPIO8 = SCL**; the P4-PC brings I²C out on its UEXT/GPIO header — so a Waveshare panel's GT911 works; you just **jumper 4 wires** (SDA, SCL, INT, RST) from the panel's touch tail to that I²C bus instead of relying on the FPC. Also budget a **reversed "type-B" 15-pin FFC** in case the DSI contact-side orientation is flipped; DSI **lane-order/polarity mismatches are fixable in ESP-IDF `esp_lcd` config** on the P4 (unlike a Raspberry Pi's fixed device-tree overlay).
- **HDMI** — `MIPI-HDMI` (Lontium LT8912B bridge) €14.95, plug-and-play with the DevKit; up to 1080p60. Proves the DSI engine against any monitor as a first bring-up step. The **P4-PC has HDMI onboard**, so it needs no adapter.
- **Camera** — `CAMERA-OV5647-5MPIX` family: €7 base, €12–13.50 for lensed/night-vision variants (1.85mm 130° wide, 2.8mm, 3.6mm), all adjustable-focus, standard 15-pin RPi CSI. Shares the OV5647 path with Waveshare + Pi.

**The strategic upshot on Olimex + Waveshare dependency.** This panel friction is inherent to *any* bring-your-own-panel board, and the concentrated finished-panel market means Olimex's display can't cleanly escape the Waveshare-origin design *today*. That argues for casting **Olimex as the radio-free / open-KiCad / PCB-reference board** — its genuinely unique value, which never depended on solving the handheld-panel problem — rather than as a daily-driver target, and using **Elecrow (§3a) as the non-Waveshare *all-in-one*** second source (integrated DSI + GT911 + camera, zero panel sourcing, and cheaper all-in than Olimex + a sourced panel). See §9.

**Pros**
- No radio, verifiable by inspection
- Open-source hardware (KiCad) — forkable for the custom board
- Sells a matching OV5647 *and* a matching DSI panel *and* an HDMI path — the port is validatable entirely on Olimex parts
- Long-established vendor with a track record of keeping products available

**Cons**
- No integrated panel, and the handheld touch-DSI-panel market is a Waveshare-origin monoculture (§7) — which is *why* it's scoped as a reference board, not a daily-driver
- Olimex's own DSI panel is 640×480 and **confirmed touchless** — usable only for DSI-engine bring-up, not the UI
- ESP-IDF only
- Historically limited to **1 board/customer** during P4 scarcity (Dec 2024). Current 2026 pages show "In Stock" with no stated limit — verify before ordering multiples.

See §10 for the exact Olimex test kit to buy.

---

## 5. Boards evaluated and set aside

Listed for completeness — these come up repeatedly in ESP32-P4 discussions and it's worth having the reasons on record.

| Board | Display | Camera | Why not |
|---|---|---|---|
| **Seeed reTerminal D1001** | 8" 800×1280 MIPI-DSI | SC2356 2MP, included | Front-facing camera; wall-panel/desktop enclosure, not handheld. $84.90. |
| **Espressif ESP32-P4-Function-EV-Board** | 7" 1024×600 (EK79007) or 1280×800 (ILI9881C) | 2MP MIPI-CSI, included | Loose panel + camera to assemble; 7" is not a handheld form factor. **Retains value as the DSI-pinout / driver reference** (both panel ICs have first-party esp_lcd drivers — useful as a neutral test panel, see §7 and §11). |
| **LILYGO T-Display P4** | 4.05" TFT / 4.1" AMOLED | Included; **AMOLED version is rear-facing** | Correct camera geometry and good handheld size, but carries C6 *plus* LoRa *plus* GPS. Recurring stock problems. $97–$119. Worth revisiting if the extra radios can be shown un-driven and stock stabilizes. |
| **ALIENTEK DNESP32P4M** | 2-lane MIPI-DSI | MIPI-CSI | **Dropped.** Radio-free and cheap, but documentation is still Chinese-only with no usable English resources and weak Western distribution — not practical to buy and support without translated docs. Track for the future; do not buy now. |

**Note:** the reTerminal and Tab5 are otherwise excellent P4 platforms and fail on *form factor / camera geometry for this application*, not on quality. The Tab5 is promoted to §3b specifically because its camera is easy to unplug and it's cheap enough to sacrifice to the experiment.

---

## 6. Camera: sensors, focus, and repositioning

This is where the anti-lock-in story is strongest, and it spans both build lines.

Espressif maintains **`esp_cam_sensor`** (part of `esp-video-components`), supporting MIPI-CSI, DVP, and SPI on the P4. Current coverage includes **OV5647, OV5640, OV2710, SC2336, SC202CS, SC035HGS, SC031IOT, GC2145, GC0308**, and others — so the Elecrow "2MP" sensor (likely SC2336) and the Tab5's SC2356 land near, though not certainly on, this list.

**IMX219 support has landed** (RAW8 1280×720 @ 60fps over MIPI-CSI on the P4). OV5647 and IMX219 are the Raspberry Pi Camera v1 (5MP) and v2 (8MP) sensors — two *different* sensors, but both are the same physical RPi-standard modules usable on the P4 with driver support. **A contributor who already owns a SeedSigner camera buys nothing new** — one component library spanning two entirely different SoCs is a more concrete demonstration of component independence than two dev boards from two vendors.

### Focus matters more than megapixels

QR capture happens 5–30cm from a laptop screen or paper. **Plain fixed-focus RPi cameras focus too far and are blurry at that range** — avoid them unless you add a macro spacer. Prefer:
- **Manual/adjustable-focus, locked at the scan distance** — deterministic, no autofocus hunting. E.g. Waveshare *RPi Camera (F)* (OV5647, rotate-and-lock lens, IR option). Best behavior for a fixed-geometry scanner.
- **Autofocus with macro** — driven to a fixed close position in software. E.g. Arducam **B0176** (OV5647, motorized AF, ~4cm macro), **B0393** (IMX219 AF, ~8cm).

### Repositioning the camera with a longer flex

For the RPi-standard **15-pin 1.0mm** boards (Waveshare, Olimex), flipping/relocating the camera is a plain cable swap:
- **Longer straight 15-pin 1.0mm flex** — low-risk to ~30cm passive, ~50cm with a quality impedance-controlled cable, >50cm needs an active extender/re-driver. Higher sensor resolution → shorter safe cable, so run the scanner at modest resolution. Multi-length packs (e.g. 15/30/50cm) are ~$10.
- **Ready-made repositionable module** — Arducam **B0066**: OV5647 on a small board with a **300mm built-in flex**, purpose-made for relocation (confirm its end connector against the board).

Caveats: connector pinout *and* pitch must match on **both** ends (board and module). This 15-pin ecosystem does **not** apply to the Tab5 (24-pin 0.5mm) or, until confirmed, the Elecrow (unpublished CSI-CAM footprint — read the Eagle schematic first).

### Organizing the supported-sensor docs

- **Split on ISP.** Some sensors have their own ISP (OV5647); others do not (SC2336/SC2356) and lean on the P4's on-chip ISP for tuning. This drives how much per-sensor tuning a new addition needs and is the right axis for the docs. (The Elecrow, Tab5, and Guition (likely SC2336) 2MP sensors are ISP-less types — they'll lean on the P4 ISP.)
- **Adding a sensor is a bounded contribution:** copy an existing driver directory, rename, implement `sensor_detect` / `sensor_set_format` / `sensor_set_stream`, plus an IPA JSON config. Each sensor is a self-contained PR that doesn't touch core code — the right shape for a community-expandable list.

---

## 7. The real lock-in risk: display panels

Vendor choice is the visible concern. Panel supply is the structural one — though less dire than first assumed now that Olimex ships a matched panel.

- **DSI connectors are not standardized.** Manufacturers implement different DSI pinouts. Olimex standardizes on the RPi 15-pin 1.0mm DSI layout; its HDMI adapter works with boards sharing that pinout (Olimex DevKit, and reportedly Waveshare NANO / DFRobot FireBeetle 2) and **breaks on boards that "flip" clock/data pins.** Adopting one convention is a real decision with real consequences.
- **Connector compatibility ≠ driver compatibility.** Several P4 boards ship RPi-style 15-pin CSI/DSI connectors, but that does not mean official Pi cameras/displays work. If the custom PCB uses RPi-style connectors, publish a *tested* panel list rather than implying Pi-ecosystem compatibility.
- **Touch rides the display FPC — but only if the board wires it.** On the RPi 15-pin DSI standard, capacitive-touch I²C is carried on the *same* FPC (pin 11 SCL, pin 12 SDA), so an integrated Waveshare touch panel needs just one cable — *on a board that routes those pins*. Olimex's ESP32-P4 boards **do not** (their DSI connector is display-only; touch I²C is a separate GPIO7/8 header on the DevKit). So treat "does this board break out touch I²C on the DSI connector?" as a per-board fact, and keep 4 jumper wires (SDA/SCL/INT/RST) on hand for boards that don't. The custom PCB should route pins 11/12 to a P4 I²C so integrated panels are truly one-cable.
- **"RPi-DSI panel" ≠ native ST7701S, and finished small panels are a concentrated market.** Many 800×480 "RPi DSI" panels (incl. the official RPi 7") use a Toshiba **TC358762 DSI→DPI bridge**, not a native-DSI controller — they won't reuse an ST7701S driver. And the ~4" 480×800 15-pin DSI *module* is largely a single Waveshare-origin design, even though the ST7701S/GT911 *ICs* are broadly multi-sourced with first-party `esp_lcd` drivers. The thing you can actually second-source is the **IC + its driver**, not a diverse finished-panel supply — one more reason the custom PCB (bare ST7701S + own FPC) is the real display-independence play. **Important distinction:** *integrated* 4.3" 480×800 ST7701S+GT911 P4 boards ARE sold by multiple unrelated vendors (Waveshare, Guition, Elecrow) — so the vendor-diversity gap bites only the *bring-your-own bare-panel* path (Olimex), not the all-in-one path. That's a strong argument for standardizing the port on the ST7701S+GT911 panel class and treating all-in-ones as the interchangeable unit.
- **Documented MIPI panels are scarce** but the driver landscape is now well-mapped. `esp_lcd` ships first-party drivers for **EK79007, HX8399, ILI9881C, JD9165, JD9365, ST7701/7703/7796/77922** — so most raw DSI panels with those controllers are bring-up-able. Known-good reference panels: Espressif EV-board **ILI9881C** (1280×800) and **EK79007** (1024×600); Waveshare 7–10.1" HMI panels use **JD9365**.

**Recommendation:** treat the panel driver IC — not the board — as the unit of support in the board-definition layer.

---

## 8. Software and toolchain constraints

- **MicroPython on the P4 is preliminary.** Vendor MicroPython runs basic commands, with interface/peripheral support not yet adapted. **MIPI-DSI and MIPI-CSI from MicroPython are not available from any vendor** — they must be built and maintained as C modules in a custom build (which this repo does).
- **Hard ESP-IDF dependency.** ESP-IDF is MicroPython's only path to the DSI/CSI peripherals. State this plainly in project docs — it's a real dependency alongside the P4 chip dependency.
- **PlatformIO does not support the ESP32-P4** (gated on PlatformIO's Arduino-ESP32 v3.1x support). Anyone expecting a PlatformIO workflow should be told up front.

---

## 9. Architectural recommendation

To make the no-lock-in claim self-evident from the repo rather than asserted in a README:

1. **Board support should be data, not code.** Panel init sequence, touch controller, sensor, pinmap, and backlight belong in a config a contributor can add. If "add a board" means editing a config file and supplying a panel init blob, the claim demonstrates itself.
2. **Keep the CSI/sensor path board-independent.** It is shared across Waveshare, Olimex, Elecrow, and the future custom PCB. Only the DSI panel path should be board-specific. Adding a vendor then costs one panel driver, not a port.
3. **Validate on a panel neither vendor sold you.** Two all-in-ones from two vendors bake in their own display stacks. Waveshare 4.3" plus an Olimex board driving a *third-party* panel (an EK79007 or ILI9881C, §7) shows the port is decoupled from any vendor's display stack — which is what skeptics will actually probe.

The full anti-lock-in stack the repo should demonstrate is three independent legs, none of which is "trust Waveshare": **(a)** an independent all-in-one vendor (**Elecrow**) proving a usable device ships without Waveshare; **(b)** commodity display/touch ICs (**ST7701S, GT911**) with **first-party `esp_lcd` drivers**, so the panel is second-sourced at the IC level; **(c)** a **radio-free open-hardware reference** (**Olimex**) forked into the custom PCB. Two dev boards from two vendors was never the real proof — this is.

---

## 10. Shopping list — what to buy

Prices observed July 2026; verify before ordering. Four tiers, roughly in priority order.

### Tier 1 — the non-Waveshare second source + the camera experiment (buy now)

Elecrow is the recommended **daily-driver second source** — independent of Waveshare, nothing to source, ~$50 all-in. The Tab5 is a cheap one-off to settle the camera-reroute question. (You already own a **Guition JC4880P443** — §3c — a ~$32 non-Waveshare 4.3" 480×800 twin of the release target; a free third comparison point, no purchase needed.)

| Item | SKU / config | Purpose | Approx. price |
|---|---|---|---|
| **Elecrow CrowPanel Advance P4 5"** | 800×480, **with 2MP camera add-on** | **Non-Waveshare second source**; test camera reposition + DSI/GT911/CSI bring-up | ≈ $50 |
| **M5Stack Tab5** | with NP-F550 battery | Assess camera-reroute DIY feasibility (unplug 24-pin FPC → rear mount) | $60 |
| *(free, do first)* | Elecrow-RD GitHub Eagle schematic | Read the `CSI-CAM` connector footprint **before** buying any cable | — |

### Tier 2 — Olimex radio-free reference + PCB-starting-point kit

Olimex's job is **radio-free bring-up + harvesting the open KiCad schematic** as the custom-PCB starting point (§4) — *not* shipping a UI (that's Elecrow). So the core buy is just the **board + camera**, plus a cheap way to light the DSI engine (onboard HDMI is free on the P4-PC, or add the €25 Olimex 2.8" panel). A full *touch* UI on Olimex is optional evaluation; if you want it, add a commodity panel + jumpers per §4.

| Item | SKU | Purpose | Price |
|---|---|---|---|
| Olimex ESP32-P4-PC | `ESP32-P4-PC` | Radio-free board; onboard HDMI + DSI + CSI; **open KiCad schematic** | €24.95 |
| Olimex camera | `CAMERA-OV5647-5MPIX-1.85mm` (base `-5MPIX` €7) | CSI camera / QR bring-up, wide + close focus | €13 |
| DSI-engine sanity | onboard HDMI (free) *or* `MIPI-LCD2.8-640x480` (ST7701S, no touch) | Prove the DSI engine lights up | — / €24.95 |
| | | **Core (board + camera + DSI sanity)** | **≈ €38–63** |
| *(opt — full UI on Olimex)* touch panel + wiring | Spotpear 4" / EK79007 + Dupont leads + type-B FFC | Run the touch UI on Olimex (evaluation only) | +$54–80 |
| *(opt)* HDMI adapter / enclosure | `MIPI-HDMI` (LT8912B) / `BOX-ESP32-P4-PC` | discrete-bridge test / case | €14.95 / €8 |

**On the panel and the Waveshare-dependency concern:** the ST7701S+GT911 *ICs* are commodities with first-party `esp_lcd` drivers, but the finished 4" 480×800 DSI panel is effectively one Waveshare-origin design (Spotpear/AliExpress are clones), so a non-Waveshare *storefront* buys convenience, not real design independence. The **EK79007 reference panel** (~$75, different IC, Espressif's own driver) is the stronger vendor-neutral proof but is 7". Genuine independence is the custom-PCB endgame (bare ST7701S + own FPC). **If escaping Waveshare-origin hardware is the priority, the cleaner move is to use Elecrow (§3a) as the non-Waveshare all-in-one and treat Olimex as the radio-free / PCB-reference board — see §4 "strategic upshot" and §9.** Verify any "RPi DSI" panel is *native* ST7701S, not a TC358762 bridge.

**Why the P4-PC and not the cheaper €16 DevKit — the sticker price is misleading.** The DevKit has no onboard HDMI, so matching the P4-PC's display-bring-up capability means adding the €14.95 `MIPI-HDMI` adapter: €16 + €14.95 = **€30.95**, which is **~€6 more** than the €24.95 P4-PC (and the P4-PC also adds 4× USB 2.0 host ports). The DevKit only wins on price if you **skip HDMI entirely** and bring up the raw ST7701S DSI panel directly — then it's €16 vs €24.95, a €9 saving. Recommendation: buy the **P4-PC** (you get the HDMI diagnostic path for less); add the `MIPI-HDMI` adapter on top only if you also want to validate the discrete LT8912B bridge chip itself.

### Tier 3 — camera / cable experiments (for the 15-pin boards: Waveshare + Olimex)

| Item | Example SKU | Purpose | Approx. price |
|---|---|---|---|
| Longer 15-pin 1.0mm flex | Pastall 6-pack (15/30/50cm) | Flip/reposition camera on Waveshare/Olimex | ~$10 |
| Close-focus AF-macro module | Arducam **B0176** (OV5647, 4cm macro) | Better close-range QR sharpness | ~$15 |
| Manual-focus module | Waveshare **RPi Camera (F)** | Deterministic locked focus for a fixed scan distance | listed |
| Ready-made repositionable | Arducam **B0066** (OV5647 + 300mm flex) | Camera-on-long-flex, purpose-built for relocation | listed |

### Tier 4 — vendor-neutral panel proof (optional, later)

| Item | Purpose |
|---|---|
| A third-party **EK79007 (1024×600)** or **ILI9881C (1280×800)** raw DSI panel | Drive from the Olimex board to prove the port is display-stack-decoupled (§9.3). Both have first-party `esp_lcd` drivers, so a failure isolates *our* port, not the panel. Sourcing/pricing of bare panels unconfirmed — check Espressif resellers / AliExpress. |

---

## 11. Test matrix — what each purchase must prove

Grouped into **known tests** (we know how to run them; they gate the buy decision) and **unknowns to resolve** (need info or bench data before we can even test).

### Elecrow 5" + camera

**Known tests**
- DSI panel + **GT911** touch bring-up in our firmware (GT911 already used on the release target — low risk).
- Identify the 2MP sensor (SC2336?) and confirm an `esp_cam_sensor` driver exists / needs adding.
- Camera default orientation; fold/pin it rearward; confirm usable QR framing and close focus.

**Unknowns to resolve first**
- `CSI-CAM` connector pinout/pitch/length from the **GitHub Eagle schematic** — determines whether a longer cable is even an option and whether any RPi-style flex applies (assume **not** until proven).
- Exact camera sensor part number (unpublished by Elecrow).

### M5Stack Tab5

**Known tests**
- Open case (hex screws), unplug the 24-pin camera FPC at J4, confirm it re-seats and streams.
- Confirm the stock flex has enough slack to reach a rear-facing position (the ribbon already runs to the top-of-board front mount, so it should).
- Fabricate a rear opening + bracket; remount the camera rear-facing using the stock flex.

**Unknowns to resolve first**
- Confirm the **24-pin FPC pitch** (schematic says 24-pin, not pitch — measure the unit) and whether the flex tail is integral or a detachable FFC.
- Internal rear clearance for the repositioned module.

*(The "swap in a replacement rear camera" path is effectively closed — no off-the-shelf module matches J4's pinout; see §3b / §12. Plan for a stock-flex reroute.)*

### Olimex reference board

**Core tests (its actual role)**
- **Radio-free inspection** — confirm no WiFi/BT silicon; the airgap-by-inspection claim, trivially verified.
- **Schematic as PCB reference** — the published KiCad is the custom-PCB starting point; sanity-check its DSI/CSI/power/storage routing against project needs.
- **DSI engine bring-up** — via onboard HDMI (cheapest) or the Olimex 2.8" panel; proves the port drives Olimex's DSI.
- **OV5647 camera bring-up** over the shared CSI path.

**Optional (only if evaluating Olimex beyond a reference board)**
- **Full touch UI** on a commodity ST7701S+GT911 panel (Spotpear/EK79007): reuse the P4-43 ST7701 driver (expect to tune DSI lane order/polarity/timing in `esp_lcd`), and wire GT911 SDA/SCL/INT/RST to the board I²C (GPIO7/8 on the DevKit; the firmware ships a GT911 driver). Confirm on the physical board whether DSI FPC pins 11/12 carry I²C (schematic shows display-only → expect to jumper) and the FPC **contact-side** orientation (may need a reversed/type-B FFC).
- Whether the discrete **LT8912B** path (DevKit) matters enough to also buy the adapter, or whether the P4-PC's onboard HDMI is sufficient.

### Cross-vendor panel proof

**Known test**
- Drive a third-party **EK79007/ILI9881C** DSI panel from the Olimex board — proves the port isn't wedded to any vendor's display stack (§9.3).

---

## 12. Open questions (remaining)

Answered since the last revision: Elecrow C6 is **soldered** (swappable slot is a separate optional-radio header); Elecrow **does** sell a matched 2MP camera add-on; Olimex **does** sell a directly-compatible ST7701S DSI panel + LT8912B HDMI adapter + OV5647 cameras.

Still open:
- [ ] Elecrow camera **sensor part number** and **CSI-CAM connector footprint** (from the GitHub Eagle schematic or a direct query) — governs any longer-cable option.
- [x] Tab5 camera-swap path — **resolved: no off-the-shelf 24-pin module is pin-compatible with J4** (CSI FPC pinouts are vendor-specific; M5 publishes no mating-connector part number and doesn't sell the camera as a spare; the reTerminal D1001's SC2356 module has an undocumented, un-purchasable pinout). Swap would need a custom/adapter flex → **reroute the stock flex instead.**
- [ ] Tab5 camera FPC **pitch** (schematic states 24-pin only) — measure the unit to confirm.
- [x] Olimex `MIPI-LCD2.8` touch — **resolved: confirmed display-only, and it's Olimex's only DSI panel.** Usable touch display = **Waveshare 4" DSI (480×800, ST7701S+GT911)**; Olimex routes no touch I²C on the DSI connector (per its KiCad schematic), so jumper the GT911 to the board's I²C (DevKit: GPIO7 SDA / GPIO8 SCL). Remaining hardware check: FPC pin-11/12 continuity + contact-side on the physical board.
- [ ] Olimex per-order limit — currently none shown, but re-check under P4 supply pressure before ordering multiples.
- [ ] Confirm the Olimex/RPi DSI pinout genuinely matches the Espressif Function-EV-Board.
- [ ] Measured close-focus distance and decode fps for OV5647 vs IMX219 in the SeedSigner scanning use case.
- [ ] Sourcing + price for bare EK79007 / ILI9881C DSI panels (vendor-neutral test).
- [x] "Can I second-source the touch DSI panel away from Waveshare?" — **resolved (nuanced): the ST7701S + GT911 *ICs* are commodities with first-party `esp_lcd` drivers, but the finished ~4" 480×800 15-pin-DSI *panel* is effectively one Waveshare-origin design (Spotpear/AliExpress clones).** Real display-independence = the EK79007 reference panel (bigger) or the custom PCB (bare ST7701S + own FPC). Cleanest escape for a daily-driver = the non-Waveshare all-in-one (Elecrow).
- [ ] Confirm the display + touch IC identity of any candidate non-Waveshare panel (Spotpear / OSOYOO / BuyDisplay pages don't name them) — and that it's **native ST7701S, not a TC358762 DSI→DPI bridge**.
- [ ] Guition JC4880P443 camera **sensor die** (FPC code "ZB2455" is internal; likely **SC2336** — confirm via I²C probe at 0x36 or the vendor schematic), and pull the **Guition doc package** (`pan.jczn1688.com/.../JC4880P443C_I_W.zip`) for the CSI pinout + panel init + confirm camera mount orientation.

---

## Source notes

Compiled from vendor product pages and documentation (Waveshare, Olimex, M5Stack, Seeed Studio, Elecrow, LILYGO, Espressif, Arducam, Adafruit), published schematics (Elecrow-RD GitHub / Eagle, M5Stack Tab5 PDF, Olimex KiCad), the Espressif Component Registry and `esp-video-components` / `esp_lcd` repositories, MIPI-CSI-2 cable-length guidance from The Imaging Source, and coverage from CNX Software, LinuxGizmos, Hackster.io, and hands-on reviews. Pricing observed July 2026; verify before publishing links or ordering. Hardware in this space is moving quickly — treat anything older than a few months as needing re-checking. Items marked *unconfirmed* in the text above were not resolvable from published sources and are the intended targets of the §11 tests.
