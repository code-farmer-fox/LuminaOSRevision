import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

CPS = [
    0x6587, 0x4EF6, 0x8BB0, 0x4E8B, 0x5173, 0x4E8E, 0x7CFB, 0x7EDF,
    0x91CD, 0x542F, 0x673A, 0x6B22, 0x8FCE, 0x4F7F, 0x7528, 0x64CD,
    0x4F5C, 0x9000, 0x51FA, 0x95ED, 0x672C,
]

THRESHOLD = 160

FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/arphic/uming.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/Library/Fonts/Songti.ttc",
    os.path.expanduser("~/Library/Fonts/Songti.ttc"),
    r"C:\Windows\Fonts\simsun.ttc",
]


def pick_font(explicit=None):
    if explicit:
        return explicit
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    raise SystemExit(
        "找不到可用字体，请把字体路径作为第二个参数传入。\n"
        "候选: " + ", ".join(FONT_CANDIDATES)
    )


def render_glyph(font, ch):
    """把单个字符渲染成 16 行 uint16。"""
    img = Image.new("L", (16, 16), color=255)
    draw = ImageDraw.Draw(img)

    # 居中对齐：用 anchor="mm" 让字形中心落在 (8, 8)
    try:
        draw.text((8, 8), ch, fill=0, font=font, anchor="mm")
    except (TypeError, ValueError):
        # 老版本 Pillow 不支持 anchor，退回到手工测量
        bbox = draw.textbbox((0, 0), ch, font=font)
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        draw.text(((16 - w) / 2 - bbox[0], (16 - h) / 2 - bbox[1]),
                  ch, fill=0, font=font)

    rows = []
    px = img.load()
    for y in range(16):
        word = 0
        for x in range(16):
            if px[x, y] < THRESHOLD:
                word |= (1 << (15 - x))  # MSB 在左
        rows.append(word)
    return rows


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(here, "..", "build", "font16.bin")

    out_path = os.path.abspath(sys.argv[1]) if len(sys.argv) >= 2 else os.path.abspath(default_out)
    font_path = pick_font(sys.argv[2] if len(sys.argv) >= 3 else None)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    # 16 像素字号；Pillow 的 size 单位是像素
    font = ImageFont.truetype(font_path, 16)

    with open(out_path, "wb") as f:
        f.write(b"LZF1")
        f.write(struct.pack("<H", len(CPS)))

        for cp in CPS:
            ch = chr(cp)
            rows = render_glyph(font, ch)
            f.write(struct.pack("<I", cp))
            for w in rows:
                f.write(struct.pack("<H", w))

    size = os.path.getsize(out_path)
    print(f"font16.bin: {size} bytes, {len(CPS)} glyphs -> {out_path}")
    print(f"  font: {font_path}")


if __name__ == "__main__":
    main()