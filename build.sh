#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build_LuminaOS"
SDK="$(cd "$ROOT/.." && pwd)/LuminaOS-SDK"

echo "=== LuminaOS Build ==="

mkdir -p "$BUILD"

echo "[1/6] Assembling bootloader..."
nasm "$ROOT/boot/boot.asm" -f bin -o "$BUILD/boot.bin" -w+all -Wno-reloc-abs-word

echo "[2/6] Assembling kernel (entry + GDT + IDT + IRQ)..."
nasm "$ROOT/kernel/arch/i386/boot.asm"    -f elf -o "$BUILD/entry.o"
nasm "$ROOT/kernel/arch/i386/gdt.asm"     -f elf -o "$BUILD/gdt_asm.o"
nasm "$ROOT/kernel/arch/i386/idt.asm"     -f elf -o "$BUILD/idt_asm.o"
nasm "$ROOT/kernel/arch/i386/irq.asm"     -f elf -o "$BUILD/irq_asm.o"
nasm "$ROOT/kernel/arch/i386/isr.asm"     -f elf -o "$BUILD/isr_asm.o"
nasm "$ROOT/kernel/arch/i386/syscall.asm" -f elf -o "$BUILD/syscall_asm.o"
nasm "$ROOT/kernel/arch/i386/usermode.asm" -f elf -o "$BUILD/usermode_asm.o"

echo "[3/6] Compiling kernel C..."
CFLAGS=(-ffreestanding -nostdlib -Wall -Wextra -I"$ROOT/kernel/include" -std=c99 -m32 -O2 -c)
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/arch/i386/gdt.c"    -o "$BUILD/gdt.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/arch/i386/idt.c"    -o "$BUILD/idt.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/arch/i386/irq.c"    -o "$BUILD/irq.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/arch/i386/isr.c"    -o "$BUILD/isr.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/tty.c"       -o "$BUILD/tty.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/keyboard.c"  -o "$BUILD/keyboard.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/system.c"    -o "$BUILD/system.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/pmm.c"       -o "$BUILD/pmm.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/paging.c"    -o "$BUILD/paging.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/heap.c"      -o "$BUILD/heap.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/ata.c"       -o "$BUILD/ata.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/fat16.c"     -o "$BUILD/fat16.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/gfx.c"       -o "$BUILD/gfx.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/mouse.c"     -o "$BUILD/mouse.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/desktop.c"   -o "$BUILD/desktop.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/syscall.c"   -o "$BUILD/syscall.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/lsp.c"       -o "$BUILD/lsp.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/timer.c"     -o "$BUILD/timer.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/sched.c"     -o "$BUILD/sched.o"
i686-elf-gcc "${CFLAGS[@]}" "$ROOT/kernel/kernel/main.c"      -o "$BUILD/main.o"

echo "[4/6] Linking kernel..."
i686-elf-ld -T "$ROOT/kernel/linker.ld" -o "$BUILD/kernel.elf" \
    "$BUILD/entry.o" "$BUILD/gdt_asm.o" "$BUILD/gdt.o" \
    "$BUILD/idt_asm.o" "$BUILD/idt.o" "$BUILD/irq_asm.o" "$BUILD/irq.o" \
    "$BUILD/isr_asm.o" "$BUILD/isr.o" \
    "$BUILD/syscall_asm.o" "$BUILD/syscall.o" "$BUILD/usermode_asm.o" \
    "$BUILD/tty.o" "$BUILD/keyboard.o" "$BUILD/system.o" "$BUILD/pmm.o" "$BUILD/paging.o" \
    "$BUILD/heap.o" "$BUILD/ata.o" "$BUILD/fat16.o" "$BUILD/gfx.o" "$BUILD/mouse.o" \
    "$BUILD/lsp.o" "$BUILD/desktop.o" "$BUILD/timer.o" "$BUILD/sched.o" "$BUILD/main.o"
i686-elf-objcopy -O binary "$BUILD/kernel.elf" "$BUILD/KERNEL.BIN"
KERNSZ=$(stat -c %s "$BUILD/KERNEL.BIN")

echo "[5/6] Creating FAT16 boot floppy (floppy.img)..."
BOOT_SIZE=$(stat -c %s "$BUILD/boot.bin")
KERNEL_SIZE=$(stat -c %s "$BUILD/KERNEL.BIN")
KC=$(( (KERNEL_SIZE + 511) / 512 ))

dd if=/dev/zero of="$BUILD/floppy.img" bs=512 count=2880 status=none

# 写入引导扇区
dd if="$BUILD/boot.bin" of="$BUILD/floppy.img" bs=1 conv=notrunc status=none

FAT_SIZE=$((12 * 512))
SECTORS_PER_FAT=12
RESERVED_SECTORS=1
NUM_FATS=2
ROOT_ENTRIES=224
ROOT_SECTORS=$(( (ROOT_ENTRIES * 32 + 511) / 512 ))
ROOT_LBA=$((RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT))
DATA_LBA=$((ROOT_LBA + ROOT_SECTORS))

# 构造 FAT 表（用 python 辅助，逻辑更清晰）
python3 - "$BUILD/floppy.img" "$RESERVED_SECTORS" "$SECTORS_PER_FAT" "$NUM_FATS" "$KC" "$ROOT_LBA" "$DATA_LBA" "$ROOT_ENTRIES" <<'PY'
import sys, struct

img, reserved, spf, num_fats, kc, root_lba, data_lba, root_entries = sys.argv[1:]
reserved = int(reserved); spf = int(spf); num_fats = int(num_fats)
kc = int(kc); root_lba = int(root_lba); data_lba = int(data_lba)
root_entries = int(root_entries)

fat = bytearray(spf * 512)
struct.pack_into('<H', fat, 0, 0xFFF8)
struct.pack_into('<H', fat, 2, 0xFFFF)
for i in range(kc):
    c = 2 + i
    nxt = 0xFFF8 if i == kc - 1 else c + 1
    struct.pack_into('<H', fat, c * 2, nxt)

with open(img, 'r+b') as f:
    for n in range(num_fats):
        f.seek((reserved + n * spf) * 512)
        f.write(fat)

    # 根目录项
    entry = bytearray(32)
    entry[0:11] = b'KERNEL  BIN'
    entry[11] = 0x20
    struct.pack_into('<H', entry, 26, 2)
    struct.pack_into('<I', entry, 28, int(sys.argv[4]) if False else 0)
    # 文件大小需从外部传入更合适，这里留待下面覆盖
PY

# 上面的 python 里文件大小不好传，改用一段专门的 python 处理根目录 + 内核写入
python3 - "$BUILD/floppy.img" "$BUILD/KERNEL.BIN" "$ROOT_LBA" "$DATA_LBA" "$KC" <<'PY'
import sys, struct

img, kernel_path, root_lba, data_lba, kc = sys.argv[1:]
root_lba = int(root_lba); data_lba = int(data_lba); kc = int(kc)

kernel = open(kernel_path, 'rb').read()

with open(img, 'r+b') as f:
    entry = bytearray(32)
    entry[0:11] = b'KERNEL  BIN'
    entry[11] = 0x20
    struct.pack_into('<H', entry, 26, 2)
    struct.pack_into('<I', entry, 28, len(kernel))
    f.seek(root_lba * 512)
    f.write(entry)

    f.seek(data_lba * 512)
    f.write(kernel)
PY

echo "[6/6] Creating FAT16 data disk (hdd.img)..."
HDD="$BUILD/hdd.img"
HDD_SECTORS=32768
HDD_SPF=128
HDD_ROOT=512

if [ -f "$HDD" ]; then
    echo "  hdd.img exists, keeping existing filesystem (delete to reformat)"
else
    BPS=512
    SPC=1
    RESERVED=1
    FAT_COUNT=2
    ROOT_ENTRIES_HDD=$HDD_ROOT
    ROOT_SECTORS_HDD=$(( (ROOT_ENTRIES_HDD * 32 + BPS - 1) / BPS ))
    SPF=$HDD_SPF
    TOTAL_SECTORS=$HDD_SECTORS
    MEDIA=0xF8

    python3 - "$HDD" "$BPS" "$SPC" "$RESERVED" "$FAT_COUNT" "$ROOT_ENTRIES_HDD" \
             "$SPF" "$TOTAL_SECTORS" "$MEDIA" <<'PY'
import sys, struct

(hdd, bps, spc, reserved, fat_count, root_entries,
 spf, total_sectors, media) = sys.argv[1:]
bps = int(bps); spc = int(spc); reserved = int(reserved); fat_count = int(fat_count)
root_entries = int(root_entries); spf = int(spf); total_sectors = int(total_sectors)
media = int(media)

bpb = bytearray(512)
bpb[0:3] = b'\xEB\x3C\x90'
bpb[3:11] = b'LUMINAHD'
struct.pack_into('<H', bpb, 11, bps)
bpb[13] = spc
struct.pack_into('<H', bpb, 14, reserved)
bpb[16] = fat_count
struct.pack_into('<H', bpb, 17, root_entries)
struct.pack_into('<H', bpb, 19, total_sectors)
bpb[21] = media
struct.pack_into('<H', bpb, 22, spf)
struct.pack_into('<H', bpb, 24, 63)
struct.pack_into('<H', bpb, 26, 255)
bpb[510] = 0x55; bpb[511] = 0xAA

fat_size = spf * bps
fat = bytearray(fat_size)
struct.pack_into('<H', fat, 0, 0xFFF8)
struct.pack_into('<H', fat, 2, 0xFFFF)

with open(hdd, 'wb') as f:
    f.write(bpb)
    f.seek(reserved * bps)
    f.write(fat)
    f.seek((reserved + spf) * bps)
    f.write(fat)
    f.truncate(total_sectors * bps)

data_lba = reserved + fat_count * spf + (root_entries * 32 + bps - 1) // bps
print(f"  hdd.img ready (data_lba={data_lba}, {total_sectors*bps} bytes)")
PY
fi

echo "=== Build complete ==="
echo "  Bootloader:  $BOOT_SIZE B"
echo "  Kernel:      $KERNSZ B ($KC clusters)"
echo "  Boot floppy: $BUILD/floppy.img (FAT16, KERNEL.BIN)"
echo "  Data disk:   $BUILD/hdd.img (FAT16, 16 MiB, persistent)"

echo "[7/7] Building apps (LSP)..."

# FAT16 hdd.img 写入工具（等价于原 Write-HddFile）
write_hdd_file() {
    local IMG="$1" NAME="$2" DATA="$3"
    python3 - "$IMG" "$NAME" "$DATA" <<'PY'
import sys, struct

img, name, data_path = sys.argv[1:]
bps = 512; spc = 1; reserved = 1; fat_count = 2
root_entries = 512; spf = 128
root_sectors = (root_entries * 32 + bps - 1) // bps
data_lba = reserved + fat_count * spf + root_sectors
root_lba = reserved + fat_count * spf

data = open(data_path, 'rb').read()
clusters = max(1, (len(data) + bps * spc - 1) // (bps * spc))

with open(img, 'r+b') as f:
    fat = bytearray(f.read(spf * bps)) if False else None
    f.seek(reserved * bps)
    fat = bytearray(f.read(spf * bps))

    def get_fat(c):
        return fat[c*2] | (fat[c*2+1] << 8)

    def set_fat(c, v):
        fat[c*2] = v & 0xFF
        fat[c*2+1] = (v >> 8) & 0xFF

    cluster_list = []
    first = 0
    for c in range(2, len(fat) // 2):
        if get_fat(c) == 0:
            if first == 0:
                first = c
            cluster_list.append(c)
            if len(cluster_list) >= clusters:
                break
    if len(cluster_list) < clusters:
        raise SystemExit("hdd.img out of space")

    for i, c in enumerate(cluster_list):
        nxt = 0xFFF8 if i == len(cluster_list) - 1 else cluster_list[i+1]
        set_fat(c, nxt)

    f.seek(reserved * bps); f.write(fat)
    f.seek((reserved + spf) * bps); f.write(fat)

    for i, c in enumerate(cluster_list):
        off = data_lba + (c - 2) * spc
        f.seek(off * bps)
        length = bps * spc
        if i == len(cluster_list) - 1:
            length = len(data) - i * length
        if length > 0:
            f.write(data[i * bps * spc : i * bps * spc + length])

    # 构建 8.3 目录项
    base = name.upper()
    if '.' in base:
        stem, ext = base.rsplit('.', 1)
    else:
        stem, ext = base, ''
    stem = stem[:8]; ext = ext[:3]
    fn = (stem + ' ' * 8)[:8].encode('ascii')
    extb = (ext + ' ' * 3)[:3].encode('ascii')

    entry = bytearray(32)
    entry[0:8] = fn
    entry[8:11] = extb
    entry[11] = 0x20
    struct.pack_into('<H', entry, 26, first)
    struct.pack_into('<I', entry, 28, len(data))

    f.seek(root_lba * bps)
    root = bytearray(f.read(root_sectors * bps))
    entry_name = (stem + ' ' * 8)[:8] + (ext + ' ' * 3)[:3]

    slot = -1
    free = -1
    for i in range(root_entries):
        cur = root[i*32:i*32+11].decode('ascii', 'replace')
        if cur == entry_name:
            slot = i
            break
        if free < 0 and (root[i*32] == 0 or root[i*32] == 0xE5):
            free = i
    if slot < 0:
        slot = free
    if slot >= 0:
        root[slot*32:slot*32+32] = entry
    f.seek(root_lba * bps)
    f.write(root)
PY
}

remove_hdd_file() {
    local IMG="$1" NAME="$2"
    [ -f "$IMG" ] || return 0
    python3 - "$IMG" "$NAME" <<'PY'
import sys

img, name = sys.argv[1:]
bps = 512; reserved = 1; fat_count = 2; root_entries = 512; spf = 128
root_sectors = (root_entries * 32 + bps - 1) // bps
root_lba = reserved + fat_count * spf

base = name.upper()
if '.' in base:
    stem, ext = base.rsplit('.', 1)
else:
    stem, ext = base, ''
stem = stem[:8]; ext = ext[:3]
entry_name = (stem + ' ' * 8)[:8] + (ext + ' ' * 3)[:3]

with open(img, 'r+b') as f:
    f.seek(root_lba * bps)
    root = bytearray(f.read(root_sectors * bps))
    slot = -1
    for i in range(root_entries):
        cur = root[i*32:i*32+11].decode('ascii', 'replace')
        if cur == entry_name:
            slot = i
            break
    if slot >= 0:
        root[slot*32] = 0xE5
        f.seek(root_lba * bps)
        f.write(root)
        print(f"  Removed {name} from hdd.img")
PY
}

ACFLAGS=(-ffreestanding -nostdlib -fno-pic -fno-stack-protector -I"$SDK/include" -m32 -O2 -c)
remove_hdd_file "$HDD" "DEMO.LSP"

for APP in "$SDK"/examples/*.c; do
    [ -e "$APP" ] || continue
    NAME="$(basename "$APP" .c)"
    OBJ="$BUILD/$NAME.o"
    ELF="$BUILD/$NAME.elf"
    BIN="$BUILD/$NAME.bin"
    LSP="$BUILD/$NAME.lsp"

    i686-elf-gcc "${ACFLAGS[@]}" "$APP" -o "$OBJ"
    i686-elf-ld -T "$SDK/app.ld" -o "$ELF" "$OBJ"
    i686-elf-objcopy -O binary "$ELF" "$BIN"

    # 找到 _start 入口地址
    ENTRY=0x10
    NM_LINE=$(i686-elf-nm "$ELF" | grep -E ' _start$' | head -n1 || true)
    if [ -n "$NM_LINE" ]; then
        ADDR=$(echo "$NM_LINE" | awk '{print $1}')
        ENTRY=$(( 0x$ADDR - 0x01000000 ))
    fi

    python3 - "$BIN" "$LSP" "$ENTRY" <<'PY'
import sys, struct

bin_path, lsp_path, entry = sys.argv[1:]
entry = int(entry)
code = open(bin_path, 'rb').read()

hdr = bytearray(16)
hdr[0:4] = b'LSP\x01'
struct.pack_into('<I', hdr, 4, entry)
struct.pack_into('<I', hdr, 8, len(code))
struct.pack_into('<I', hdr, 12, 0)

with open(lsp_path, 'wb') as f:
    f.write(hdr + code)
PY
    IMG_SIZE=$(stat -c %s "$LSP")
    echo "  $NAME.lsp: $IMG_SIZE B"
    write_hdd_file "$HDD" "$NAME.LSP" "$LSP"
done

echo "[8/8] CJK font..."
FONT_BIN="$BUILD/font16.bin"
if python3 "$ROOT/tools/gen_font.py" "$FONT_BIN"; then
    pip3 install Pillow
    FONT_SIZE=$(stat -c %s "$FONT_BIN")
    remove_hdd_file "$HDD" "FONT16.BIN"
    write_hdd_file "$HDD" "FONT16.BIN" "$FONT_BIN"
    echo "  FONT16.BIN: $FONT_SIZE B"
fi

echo "=== Build complete ==="