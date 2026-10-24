#!/usr/bin/env python3
"""make-icons.py [outdir] -- generate AmiForge's Workbench icons (default: dist/icons).

Icons are classic DiskObjects (what icon.library reads on every AmigaOS) carrying a 4-colour
planar fallback image, followed by an IFF FORM ICON with a FACE chunk and two ARGB chunks
(normal and selected 64x64 true-colour images, zlib-compressed) as used by AmigaOS 4.

  AmiForge.info         tool icon for the IDE binary
  AmiForge-drawer.info  the AmiForge drawer (rename to AmiForge.info beside the drawer)
  drawer.info           plain drawer (Examples and the example drawers)
  project.info          .afp project; default tool //AmiForge (two levels up)
  doc.info              text documents; default tool SYS:Utilities/MultiView
  install.info          Install-AmiForge script; default tool C:IconX

Needs Pillow. Images are drawn at 4x and downsampled, so they come out antialiased.
"""
import os
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SIZE = 64          # icon size in pixels
SS = 4             # supersampling factor

# --- drawing helpers (coordinates in icon pixels) ---------------------------------------

def canvas():
    return Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))


def s(points):
    return [(x * SS, y * SS) for x, y in points]


def gradient_fill(img, mask, top, bottom, y0, y1):
    """Fill the mask with a vertical gradient from colour top (at y0) to bottom (at y1)."""
    grad = Image.new("RGBA", img.size)
    gd = ImageDraw.Draw(grad)
    for y in range(img.size[1]):
        t = min(1.0, max(0.0, (y / SS - y0) / max(1e-6, y1 - y0)))
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(4))
        gd.line([(0, y), (img.size[0], y)], fill=c)
    img.paste(Image.alpha_composite(img, Image.composite(grad, Image.new("RGBA", img.size), mask)))


def polygon_mask(points):
    m = Image.new("L", (SIZE * SS, SIZE * SS), 0)
    ImageDraw.Draw(m).polygon(s(points), fill=255)
    return m


def font(size):
    for path, index in (("/System/Library/Fonts/Menlo.ttc", 1),
                        ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 0)):
        if os.path.exists(path):
            return ImageFont.truetype(path, int(size * SS), index=index)
    return ImageFont.load_default()


def drop_shadow(img, offset=(1.5, 2.0), blur=1.6, alpha=110):
    """Composite a soft shadow of img's alpha under img."""
    a = img.split()[3].filter(ImageFilter.GaussianBlur(blur * SS))
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shadow.putalpha(a.point(lambda v: v * alpha // 255))
    moved = Image.new("RGBA", img.size, (0, 0, 0, 0))
    moved.paste(shadow, (int(offset[0] * SS), int(offset[1] * SS)))
    return Image.alpha_composite(moved, img)


# --- motifs -------------------------------------------------------------------------------

ANVIL = [(4, 27), (9, 24.5), (17, 23), (53, 23), (57, 24), (57, 30), (50, 31.5), (45, 33.5),
         (43, 40), (49, 43), (51, 49), (15, 49), (17, 43), (23, 40), (21, 33.5), (15, 31.5),
         (9, 30), (5, 28.5)]


def transform(points, scale, dx, dy):
    return [(x * scale + dx, y * scale + dy) for x, y in points]


def draw_anvil(img, scale=1.0, dx=0, dy=0):
    pts = transform(ANVIL, scale, dx, dy)
    mask = polygon_mask(pts)
    y0, y1 = 23 * scale + dy, 49 * scale + dy
    gradient_fill(img, mask, (150, 162, 178, 255), (40, 46, 56, 255), y0, y1)
    d = ImageDraw.Draw(img)
    # bright top face and outline
    top = transform([(9, 24.5), (17, 23), (53, 23), (57, 24), (56, 26), (17, 25.5), (8, 26.5)], scale, dx, dy)
    d.polygon(s(top), fill=(214, 222, 232, 255))
    d.line(s(pts + [pts[0]]), fill=(22, 26, 32, 255), width=max(1, int(1.0 * SS * scale)), joint="curve")


def draw_braces(img, size=22, cx=31, cy=12, glow=True):
    """An orange-hot "{ }" with a glow, as if fresh from the forge."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    f = font(size)
    text = "{ }"
    box = d.textbbox((0, 0), text, font=f)
    x = cx * SS - (box[0] + box[2]) / 2
    y = cy * SS - (box[1] + box[3]) / 2
    d.text((x, y), text, font=f, fill=(255, 140, 26, 255))
    if glow:
        g = layer.filter(ImageFilter.GaussianBlur(2.2 * SS))
        img.paste(Image.alpha_composite(img, g))
        img.paste(Image.alpha_composite(img, g))
    # hot core: lighter yellow inside the strokes
    core = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(core).text((x, y - 0.5 * SS), text, font=f, fill=(255, 214, 120, 255))
    core.putalpha(core.split()[3].point(lambda v: v * 3 // 5))
    img.paste(Image.alpha_composite(img, layer))
    img.paste(Image.alpha_composite(img, core))


def draw_sparks(img, sparks):
    d = ImageDraw.Draw(img)
    for x, y, r in sparks:
        d.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=(255, 200, 90, 230))


def draw_page(img, fold=10):
    """A sheet of paper with a folded top-right corner."""
    x0, y0, x1, y1 = 12, 3, 52, 61
    body = [(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)]
    gradient_fill(img, polygon_mask(body), (255, 255, 255, 255), (222, 228, 236, 255), y0, y1)
    d = ImageDraw.Draw(img)
    d.line(s(body + [body[0]]), fill=(120, 130, 145, 255), width=SS, joint="curve")
    corner = [(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)]
    d.polygon(s(corner), fill=(196, 205, 218, 255), outline=(120, 130, 145, 255))


def draw_drawer(img):
    """An AmigaOS 4 style blue folder."""
    back = [(5, 12), (24, 12), (28, 16), (59, 16), (59, 54), (5, 54)]
    gradient_fill(img, polygon_mask(back), (70, 118, 186, 255), (38, 74, 132, 255), 12, 54)
    paper = [(9, 19), (55, 19), (55, 40), (9, 40)]
    ImageDraw.Draw(img).polygon(s(paper), fill=(246, 246, 240, 255))
    front = [(3, 26), (61, 26), (58, 57), (6, 57)]
    gradient_fill(img, polygon_mask(front), (136, 182, 240, 255), (60, 110, 186, 255), 26, 57)
    d = ImageDraw.Draw(img)
    d.line(s(front + [front[0]]), fill=(26, 52, 98, 255), width=SS, joint="curve")
    d.line(s([(5, 12), (24, 12), (28, 16), (59, 16), (59, 26)]), fill=(26, 52, 98, 255), width=SS)
    d.line(s([(5, 12), (5, 26)]), fill=(26, 52, 98, 255), width=SS)
    d.line(s([(5, 28), (59, 28)]), fill=(190, 220, 255, 200), width=SS)   # front lip highlight


# --- the icons ----------------------------------------------------------------------------

def finish(img):
    img = drop_shadow(img)
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def icon_tool():
    img = canvas()
    draw_anvil(img, 1.0, 0, 6)
    draw_braces(img, 23, 31, 14)
    draw_sparks(img, [(9, 16, 1.0), (54, 12, 1.2), (49, 21, 0.8), (13, 9, 0.7)])
    return finish(img)


def icon_drawer(badge=True):
    img = canvas()
    draw_drawer(img)
    if badge:
        draw_anvil(img, 0.5, 16, 25)
        draw_braces(img, 13, 32, 36, glow=True)
    return finish(img)


def icon_project():
    img = canvas()
    draw_page(img)
    d = ImageDraw.Draw(img)
    for i, w in enumerate((22, 30, 18, 26)):
        y = 12 + i * 5
        d.line([(17 * SS, y * SS), ((17 + w) * SS, y * SS)], fill=(150, 162, 180, 255), width=int(1.6 * SS))
    draw_anvil(img, 0.55, 15, 25)
    draw_braces(img, 12, 32, 38)
    return finish(img)


def icon_doc():
    img = canvas()
    draw_page(img)
    d = ImageDraw.Draw(img)
    for i in range(9):
        y = 16 + i * 5
        w = (30, 34, 26, 34, 32, 20, 34, 28, 16)[i]
        d.line([(17 * SS, y * SS), ((17 + w * 0.94) * SS, y * SS)], fill=(110, 122, 140, 255), width=int(1.5 * SS))
    d.rectangle([17 * SS, 8 * SS, 33 * SS, 11 * SS], fill=(58, 104, 170, 255))   # title bar
    return finish(img)


def icon_install():
    img = canvas()
    draw_page(img)
    # a green arrow down into a tray
    arrow = [(27, 10), (37, 10), (37, 24), (44, 24), (32, 36), (20, 24), (27, 24)]
    gradient_fill(img, polygon_mask(arrow), (120, 220, 120, 255), (30, 140, 50, 255), 10, 36)
    d = ImageDraw.Draw(img)
    d.line(s(arrow + [arrow[0]]), fill=(18, 80, 30, 255), width=SS, joint="curve")
    d.line(s([(18, 40), (18, 46), (46, 46), (46, 40)]), fill=(70, 80, 95, 255), width=int(2 * SS))
    draw_anvil(img, 0.42, 19, 37)
    return finish(img)


def selected(img):
    """The selected image: darker, with a blue glow behind (OS4 style)."""
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    halo = img.split()[3].filter(ImageFilter.GaussianBlur(3)).point(lambda v: min(255, v * 2))
    glow.paste((90, 150, 255, 255), mask=halo)
    dark = img.copy()
    px = dark.load()
    for y in range(dark.size[1]):
        for x in range(dark.size[0]):
            r, g, b, a = px[x, y]
            px[x, y] = (r * 3 // 4, g * 3 // 4, b * 3 // 4 + 30 if a else 0, a)
    return Image.alpha_composite(glow, dark)


# --- icon file format ---------------------------------------------------------------------

WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4
NO_ICON_POSITION = 0x80000000
PALETTE = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)]   # Workbench pens 0-3


def planar(img):
    """A 2-bitplane image for old icon.library versions: nearest Workbench pen per pixel."""
    w, h = img.size
    row_bytes = (w + 15) // 16 * 2
    planes = [bytearray(row_bytes * h) for _ in range(2)]
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                pen = 0
            else:
                pen = min(range(1, 4), key=lambda p: sum((c - q) ** 2 for c, q in zip((r, g, b), PALETTE[p])))
            for p in range(2):
                if pen >> p & 1:
                    planes[p][y * row_bytes + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0)   # struct Image
    return header + bytes(planes[0]) + bytes(planes[1])


def text_block(t):
    data = t.encode("latin-1") + b"\0"
    return struct.pack(">I", len(data)) + data


def argb_chunk(img):
    rgba = img.tobytes()
    raw = bytearray(len(rgba))
    raw[0::4], raw[1::4], raw[2::4], raw[3::4] = rgba[3::4], rgba[0::4], rgba[1::4], rgba[2::4]
    packed = zlib.compress(bytes(raw), 9)
    # header as written by OS4's icon.library: 1, compressed size - 1, 0
    body = struct.pack(">IIH", 1, len(packed) - 1, 0) + packed
    return b"ARGB" + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def iff_icon(normal, sel):
    # width-1, height-1, flags, aspect, max palette bytes: the values OS4's own true-colour
    # icons carry (icon.library rejects the icon with flags 0x01 and no palette)
    face = struct.pack(">BBBBH", SIZE - 1, SIZE - 1, 0x02, 0x00, 1)
    chunks = b"FACE" + struct.pack(">I", len(face)) + face + argb_chunk(normal) + argb_chunk(sel)
    return b"FORM" + struct.pack(">I", len(chunks) + 4) + b"ICON" + chunks


def disk_object(kind, img, default_tool=None, tool_types=(), stack=0, drawer=None):
    sel = selected(img)
    gadget = struct.pack(">IhhhhHHHIIIiIHI",
                         0, 0, 0, SIZE, SIZE,
                         0x0006,      # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
                         0x0001,      # GACT_RELVERIFY
                         0x0001,      # GTYP_BOOLGADGET
                         1, 1, 0, 0, 0, 0,
                         1)           # UserData: WB_DISKREVISION 1 (OS 2.x+ icon)
    obj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIIIIII", kind, 0,
        1 if default_tool else 0, 1 if tool_types else 0,
        NO_ICON_POSITION, NO_ICON_POSITION,
        1 if drawer else 0, 0, stack)
    out = bytearray(obj)
    if drawer:
        left, top, width, height = drawer
        # OldDrawerData: struct NewWindow, then dd_CurrentX/Y
        out += struct.pack(">hhhhBBIIIIIIIhhHHH", left, top, width, height, 255, 255,
                           0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
        out += struct.pack(">ii", 0, 0)
    out += planar(img) + planar(sel)
    if default_tool:
        out += text_block(default_tool)
    if tool_types:
        out += struct.pack(">I", (len(tool_types) + 1) * 4)
        for t in tool_types:
            out += text_block(t)
    if drawer:
        out += struct.pack(">IH", 1, 1)     # dd_Flags: show icons only; dd_ViewModes: by icon
    return bytes(out) + iff_icon(img, sel)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "dist", "icons")
    os.makedirs(outdir, exist_ok=True)
    icons = {
        "AmiForge.info": disk_object(WBTOOL, icon_tool(), stack=524288),
        "AmiForge-drawer.info": disk_object(WBDRAWER, icon_drawer(True), drawer=(80, 60, 460, 260)),
        "drawer.info": disk_object(WBDRAWER, icon_drawer(False), drawer=(100, 80, 420, 220)),
        "project.info": disk_object(WBPROJECT, icon_project(), default_tool="//AmiForge", stack=524288),
        "doc.info": disk_object(WBPROJECT, icon_doc(), default_tool="SYS:Utilities/MultiView", stack=65536),
        "install.info": disk_object(WBPROJECT, icon_install(), default_tool="C:IconX", stack=65536,
                                    tool_types=("WINDOW=CON:80/80/560/200/Install AmiForge/AUTO/CLOSE/WAIT",)),
    }
    for name, data in icons.items():
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
        print(f"{name:22s} {len(data):6d} bytes")


if __name__ == "__main__":
    main()
