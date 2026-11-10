"""Draws the GitHub artwork in branding/github from the app icon's S-and-faders mark.

Run from the repository root: python tools/make_branding.py (needs Pillow and fontTools).
"""
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

OUT = Path("branding/github")
BOLD = "resources/fonts/BarlowCondensed-SemiBold.ttf"
MEDIUM = "resources/fonts/BarlowCondensed-Medium.ttf"
ORANGE, CAP, INK, WHITE = "#FF9220", "#EEF0F3", "#3A3F48", "#F3F5F7"
TOP, BOTTOM = (56, 61, 71), (22, 25, 30)
SS = 4  # supersampling

# The mark, in its own 204 x 192 box.
POINTS = [(169, 14), (35, 14), (35, 96), (168, 96), (168, 177), (34, 177)]
STROKE = 28
CAPS = [(0, 42, 69, 30), (135, 122, 69, 30)]


def draw_mark(d, x, y, s):
    pts = [(x + px * s, y + py * s) for px, py in POINTS]
    w = round(STROKE * s)
    d.line(pts, fill=ORANGE, width=w, joint="curve")
    for p in pts:
        d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=ORANGE)
    for cx, cy, cw, ch in CAPS:
        d.rounded_rectangle((x + cx * s, y + cy * s, x + (cx + cw) * s, y + (cy + ch) * s), radius=6 * s, fill=CAP)
        ly = y + (cy + ch / 2) * s
        d.line((x + (cx + 9) * s, ly, x + (cx + cw - 9) * s, ly), fill=INK, width=max(1, round(3.5 * s)))


def gradient(w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for yy in range(h):
        t = yy / max(1, h - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM))
        for xx in range(w):
            px[xx, yy] = c
    return im


def spaced(d, x, y, text, font, fill, spacing):
    for ch in text:
        d.text((x, y), ch, font=font, fill=fill, anchor="ls")
        x += d.textlength(ch, font=font) + spacing
    return x - spacing




def banner(w, h, name, title_px, tag):
    im = gradient(w * SS, h * SS)
    d = ImageDraw.Draw(im)
    s = h * 0.46 / 192
    mw = 204 * s
    title = ImageFont.truetype(BOLD, title_px * SS)
    sub = ImageFont.truetype(MEDIUM, round(title_px * 0.26) * SS)
    gap = 0.11 * title_px
    tw = sum(d.textlength(c, font=title) for c in "SIDECAR") / SS + gap * 6
    left = (w - mw - 0.5 * title_px - tw) / 2
    my = (h - 192 * s) / 2 - h * 0.01
    draw_mark(d, left * SS, my * SS, s * SS)
    tx = left + mw + 0.5 * title_px
    base = h * 0.5 + title_px * 0.2
    spaced(d, tx * SS, base * SS, "SIDECAR", title, WHITE, gap * SS)
    spaced(d, tx * SS, (base + title_px * 0.44) * SS, tag, sub, ORANGE, title_px * 0.045 * SS)
    d.rectangle((0, (h - max(6, h // 54)) * SS, w * SS, h * SS), fill=ORANGE)
    im.resize((w, h), Image.LANCZOS).save(OUT / name, optimize=True)


def icon(size, name, padded=True):
    im = gradient(size * SS, size * SS)
    d = ImageDraw.Draw(im)
    s = size * (0.68 if padded else 0.8) / 204
    draw_mark(d, (size - 204 * s) / 2 * SS, (size - 192 * s) / 2 * SS, s * SS)
    im.resize((size, size), Image.LANCZOS).save(OUT / name, optimize=True)


def mark_svg():
    d = "M" + " L".join(f"{x} {y}" for x, y in POINTS)
    parts = [f'<path d="{d}" fill="none" stroke="{ORANGE}" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round"/>']
    for cx, cy, cw, ch in CAPS:
        parts.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="6" fill="{CAP}"/>')
        parts.append(f'<rect x="{cx + 9}" y="{cy + ch / 2 - 1.75}" width="{cw - 18}" height="3.5" fill="{INK}"/>')
    return "\n  ".join(parts)


def text_path(text, size, spacing, x, y):
    font = TTFont(BOLD)
    glyphs, cmap, adv = font.getGlyphSet(), font.getBestCmap(), font["hmtx"]
    k = size / font["head"].unitsPerEm
    out = []
    for ch in text:
        name = cmap[ord(ch)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(pen)
        out.append(f'<path transform="translate({x:.2f} {y}) scale({k:.5f} {-k:.5f})" d="{pen.getCommands()}"/>')
        x += adv[name][0] * k + spacing
    return out, x - spacing


def write_svgs():
    (OUT / "sidecar-mark.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 204 192" role="img" aria-label="Sidecar">\n  {mark_svg()}\n</svg>\n',
        encoding="utf8")
    for name, fill in (("sidecar-logo-on-dark.svg", WHITE), ("sidecar-logo-on-light.svg", "#1C2026")):
        paths, end = text_path("SIDECAR", 150, 16, 262, 148)
        body = "\n    ".join(paths)
        (OUT / name).write_text(
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {end + 4:.0f} 192" role="img" aria-label="Sidecar">\n'
            f'  {mark_svg()}\n  <g fill="{fill}">\n    {body}\n  </g>\n</svg>\n', encoding="utf8")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_svgs()
    banner(1280, 640, "sidecar-social-preview.png", 190, "LIVE MIXING CONSOLES")
    banner(1280, 320, "sidecar-readme-banner.png", 130, "LIVE MIXING CONSOLES")
    icon(500, "sidecar-avatar-500.png")
    icon(64, "sidecar-favicon-64.png", padded=False)


main()
