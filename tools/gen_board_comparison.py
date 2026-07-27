#!/usr/bin/env python3
"""Generate the supported-board comparison chart (color-coded PNG).

Data-driven: edit BOARDS / STATUS / PRICE / CHIP / RATINGS / NOTES below and re-run
to regenerate the image. Quality cells are shaded on a continuous green→red ramp so
a mostly-green row reads as a strong board at a glance. No external services —
just Pillow.

    python3 tools/gen_board_comparison.py

Writes docs/img/board-comparison.png (referenced from docs/supported-hardware.md).
"""
import os
from PIL import Image, ImageDraw, ImageFont

# ── Data (edit me) ──────────────────────────────────────────────────────────
BOARDS = ["Waveshare 4.3", "Guition 4.3", "Waveshare 3.5", "Waveshare 5", "Waveshare S3 3.5B"]
RECOMMENDED = {"Waveshare 4.3"}          # gets a ★ and is called out in the doc

STATUS = {
    "Waveshare 4.3": "Supported", "Guition 4.3": "Supported", "Waveshare 3.5": "Supported",
    "Waveshare 5": "Planned", "Waveshare S3 3.5B": "Under eval.",
}
PRICE = {
    "Waveshare 4.3": "$43", "Guition 4.3": "$35", "Waveshare 3.5": "$38",
    "Waveshare 5": "$53", "Waveshare S3 3.5B": "—",
}
# Chip / platform. P4 = best (green); S3 = not yet assessed (gray).
CHIP = {
    "Waveshare 4.3": "P4", "Guition 4.3": "P4", "Waveshare 3.5": "P4",
    "Waveshare 5": "P4", "Waveshare S3 3.5B": "S3",
}

DIMENSIONS = ["Touchscreen", "Live preview", "Camera", "Reputation"]

# Quality scale (drives the green→red ramp). NA = not evaluated (gray, off-ramp).
BEST, GOOD, FAIR, POOR, NA = 4, 3, 2, 1, 0
RATING_LABEL = {BEST: "Best", GOOD: "Good", FAIR: "Fair", POOR: "Poor", NA: "n/a"}

# Some gray (NA) cells mean "unknown / unestablished", not "untested" — override the label.
NA_LABEL = {("Guition 4.3", "Reputation"): "Unknown"}

RATINGS = {
    "Waveshare 4.3":      {"Touchscreen": BEST, "Live preview": BEST, "Camera": BEST, "Reputation": BEST},
    "Guition 4.3":        {"Touchscreen": BEST, "Live preview": BEST, "Camera": POOR, "Reputation": NA},
    "Waveshare 3.5":      {"Touchscreen": FAIR, "Live preview": FAIR, "Camera": FAIR, "Reputation": BEST},
    "Waveshare 5":        {"Touchscreen": NA,   "Live preview": NA,   "Camera": NA,   "Reputation": BEST},
    "Waveshare S3 3.5B":  {"Touchscreen": FAIR, "Live preview": NA,   "Camera": NA,   "Reputation": BEST},
}

TITLE = "SeedSigner ESP32 board comparison"
NOTES = [
    "Waveshare 5 has not been received yet — unevaluated (manufacturer reputation aside).",
    "Touchscreen: the 3.5\" boards need a firmer, more deliberate press than the 4.3\" boards.",
    "Chip: the ESP32-P4 boards are the faster platform; the ESP32-S3's real-world performance is not yet assessed.",
    "Camera: Waveshare 4.3 (OV5647) > Waveshare 3.5 (same sensor, lower resolution) > Guition (OV02C10).",
    "Reputation: Waveshare is well-established; Guition is new and largely unknown — unestablished, not known-bad.",
    "Price is the bare-board list price (July 2026); a mostly-green row is the stronger board.",
]

OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "docs", "img", "board-comparison.png")

# ── Colors ──────────────────────────────────────────────────────────────────
RED, YELLOW, GREEN = (208, 48, 45), (245, 191, 66), (43, 130, 52)
GRAY, LIGHT, HEADER_BG, INK, MUTED, BG = (198, 202, 206), (245, 246, 248), (236, 238, 241), (33, 37, 41), (92, 99, 107), (255, 255, 255)
STATUS_COLOR = {"Supported": GREEN, "Planned": YELLOW, "Under eval.": GRAY}


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def quality_color(level):
    """1..4 → red→yellow→green ramp; NA → gray."""
    if level == NA:
        return GRAY
    t = (level - POOR) / (BEST - POOR)                 # 0..1
    return lerp(RED, YELLOW, t / 0.5) if t < 0.5 else lerp(YELLOW, GREEN, (t - 0.5) / 0.5)


def text_on(rgb):
    lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]
    return (25, 25, 25) if lum > 150 else (255, 255, 255)


# ── Layout ──────────────────────────────────────────────────────────────────
S = 2                       # supersample, downscaled at the end
PAD = 22 * S
TITLE_H = 42 * S
HEADER_H = 44 * S
ROW_H = 48 * S
GAP = 3 * S
BOARD_W = 196 * S
LEGEND_H = 52 * S
NOTE_H = 21 * S
# meta + dimension columns: (label, width, kind)
COLS = ([("Status", 104 * S, "status"), ("Price", 68 * S, "price"), ("Chip", 72 * S, "chip")]
        + [(d, 118 * S, "qual") for d in DIMENSIONS])

FONT_DIRS = ["", "/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/dejavu/"]


def font(size, bold=False):
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    for d in FONT_DIRS:
        try:
            return ImageFont.truetype(d + name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def center(draw, box, text, fnt, fill):
    l, t, r, b = draw.textbbox((0, 0), text, font=fnt)
    draw.text((box[0] + (box[2] - box[0] - (r - l)) / 2 - l,
               box[1] + (box[3] - box[1] - (b - t)) / 2 - t), text, font=fnt, fill=fill)


def main():
    grid_w = BOARD_W + sum(w for _, w, _ in COLS)
    grid_h = HEADER_H + len(BOARDS) * ROW_H
    W = PAD * 2 + grid_w
    H = PAD + TITLE_H + grid_h + LEGEND_H + len(NOTES) * NOTE_H + PAD
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    f_title, f_head, f_cell, f_note = font(24 * S, 1), font(14 * S, 1), font(14 * S, 1), font(12 * S)

    d.text((PAD, PAD - 2 * S), TITLE, font=f_title, fill=INK)
    gx, gy = PAD, PAD + TITLE_H

    # Column headers.
    x = gx + BOARD_W
    for label, w, _ in COLS:
        d.rectangle([x + GAP, gy, x + w, gy + HEADER_H], fill=HEADER_BG)
        center(d, (x + GAP, gy, x + w, gy + HEADER_H), label, f_head, INK)
        x += w

    # Rows.
    for r, board in enumerate(BOARDS):
        y = gy + HEADER_H + r * ROW_H
        name = ("★ " if board in RECOMMENDED else "") + board
        d.rectangle([gx, y + GAP, gx + BOARD_W, y + ROW_H], fill=HEADER_BG)
        l, t, rr, bb = d.textbbox((0, 0), name, font=f_head)
        d.text((gx + 10 * S, y + GAP + (ROW_H - GAP - (bb - t)) / 2 - t), name, font=f_head, fill=INK)
        x = gx + BOARD_W
        for label, w, kind in COLS:
            box = (x + GAP, y + GAP, x + w, y + ROW_H)
            if kind == "status":
                col = STATUS_COLOR[STATUS[board]]
                d.rectangle(box, fill=col)
                center(d, box, STATUS[board], f_cell, text_on(col))
            elif kind == "price":
                d.rectangle(box, fill=LIGHT)
                center(d, box, PRICE[board], f_cell, INK)
            elif kind == "chip":
                col = GREEN if CHIP[board] == "P4" else GRAY
                d.rectangle(box, fill=col)
                center(d, box, CHIP[board], f_cell, text_on(col))
            else:
                lvl = RATINGS[board][label]
                col = quality_color(lvl)
                d.rectangle(box, fill=col)
                center(d, box, NA_LABEL.get((board, label), RATING_LABEL[lvl]), f_cell, text_on(col))
            x += w

    # Legend: green→red gradient bar + gray swatch.
    ly = gy + grid_h + 14 * S
    bar_w, bar_h = 150 * S, 16 * S
    for i in range(bar_w):
        t = i / (bar_w - 1)                      # 0=green(Best) .. 1=red(Poor)
        col = lerp(GREEN, YELLOW, t / 0.5) if t < 0.5 else lerp(YELLOW, RED, (t - 0.5) / 0.5)
        d.line([(gx + i, ly), (gx + i, ly + bar_h)], fill=col)
    d.text((gx, ly + bar_h + 4 * S), "Best", font=f_note, fill=MUTED)
    rl = d.textbbox((0, 0), "Poor", font=f_note)
    d.text((gx + bar_w - (rl[2] - rl[0]), ly + bar_h + 4 * S), "Poor", font=f_note, fill=MUTED)
    d.text((gx + bar_w / 2 - 10 * S, ly + bar_h + 4 * S), "quality", font=f_note, fill=MUTED)
    # gray = not evaluated
    gxx = gx + bar_w + 40 * S
    d.rectangle([gxx, ly, gxx + bar_h, ly + bar_h], fill=GRAY)
    d.text((gxx + bar_h + 6 * S, ly + 1 * S), "not evaluated / unknown", font=f_note, fill=MUTED)

    # Notes.
    ny = ly + LEGEND_H - 4 * S
    for note in NOTES:
        d.text((gx, ny), "• " + note, font=f_note, fill=MUTED)
        ny += NOTE_H

    img = img.resize((W // S, H // S), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    img.save(OUT_PATH)
    print("wrote", os.path.normpath(OUT_PATH))


if __name__ == "__main__":
    main()
