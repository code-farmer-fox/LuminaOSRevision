$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot
$BUILD = "$ROOT\build_LuminaOS"
$SDK = Join-Path (Split-Path $ROOT -Parent) "LuminaOS-SDK"

Write-Host "=== LuminaOS Build ===" -ForegroundColor Cyan

New-Item -ItemType Directory -Path $BUILD -Force | Out-Null

Write-Host "[1/6] Assembling bootloader..."
nasm "$ROOT\boot\boot.asm" -f bin -o "$BUILD\boot.bin" -w+all -Wno-reloc-abs-word

Write-Host "[2/6] Assembling kernel (entry + GDT + IDT + IRQ)..."
nasm "$ROOT\kernel\arch\i386\boot.asm" -f elf -o "$BUILD\entry.o"
nasm "$ROOT\kernel\arch\i386\gdt.asm" -f elf -o "$BUILD\gdt_asm.o"
nasm "$ROOT\kernel\arch\i386\idt.asm" -f elf -o "$BUILD\idt_asm.o"
nasm "$ROOT\kernel\arch\i386\irq.asm" -f elf -o "$BUILD\irq_asm.o"
nasm "$ROOT\kernel\arch\i386\isr.asm" -f elf -o "$BUILD\isr_asm.o"
nasm "$ROOT\kernel\arch\i386\syscall.asm" -f elf -o "$BUILD\syscall_asm.o"
nasm "$ROOT\kernel\arch\i386\usermode.asm" -f elf -o "$BUILD\usermode_asm.o"

Write-Host "[3/6] Compiling kernel C..."
$cflags = "-ffreestanding", "-nostdlib", "-Wall", "-Wextra", "-I$ROOT\kernel\include", "-std=c99", "-m32", "-O2", "-c"
i686-elf-gcc @cflags "$ROOT\kernel\arch\i386\gdt.c" -o "$BUILD\gdt.o"
i686-elf-gcc @cflags "$ROOT\kernel\arch\i386\idt.c" -o "$BUILD\idt.o"
i686-elf-gcc @cflags "$ROOT\kernel\arch\i386\irq.c" -o "$BUILD\irq.o"
i686-elf-gcc @cflags "$ROOT\kernel\arch\i386\isr.c" -o "$BUILD\isr.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\tty.c" -o "$BUILD\tty.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\keyboard.c" -o "$BUILD\keyboard.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\system.c" -o "$BUILD\system.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\pmm.c" -o "$BUILD\pmm.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\paging.c" -o "$BUILD\paging.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\heap.c" -o "$BUILD\heap.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\ata.c" -o "$BUILD\ata.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\fat16.c" -o "$BUILD\fat16.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\gfx.c" -o "$BUILD\gfx.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\mouse.c" -o "$BUILD\mouse.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\desktop.c" -o "$BUILD\desktop.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\syscall.c" -o "$BUILD\syscall.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\lsp.c" -o "$BUILD\lsp.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\timer.c" -o "$BUILD\timer.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\sched.c" -o "$BUILD\sched.o"
i686-elf-gcc @cflags "$ROOT\kernel\kernel\main.c" -o "$BUILD\main.o"

Write-Host "[4/6] Linking kernel..."
i686-elf-ld -T "$ROOT\kernel\linker.ld" -o "$BUILD\kernel.elf" `
    "$BUILD\entry.o" "$BUILD\gdt_asm.o" "$BUILD\gdt.o" `
    "$BUILD\idt_asm.o" "$BUILD\idt.o" "$BUILD\irq_asm.o" "$BUILD\irq.o" `
    "$BUILD\isr_asm.o" "$BUILD\isr.o" `
    "$BUILD\syscall_asm.o" "$BUILD\syscall.o" "$BUILD\usermode_asm.o" `
    "$BUILD\tty.o" "$BUILD\keyboard.o" "$BUILD\system.o" "$BUILD\pmm.o" "$BUILD\paging.o" "$BUILD\heap.o" "$BUILD\ata.o" "$BUILD\fat16.o" "$BUILD\gfx.o" "$BUILD\mouse.o" "$BUILD\lsp.o" "$BUILD\desktop.o" "$BUILD\timer.o" "$BUILD\sched.o" "$BUILD\main.o"
i686-elf-objcopy -O binary "$BUILD\kernel.elf" "$BUILD\KERNEL.BIN"
$kernSz = (Get-Item "$BUILD\KERNEL.BIN").Length

Write-Host "[5/6] Creating FAT16 boot floppy (floppy.img)..."
$boot = [System.IO.File]::ReadAllBytes("$BUILD\boot.bin")
$kernel = [System.IO.File]::ReadAllBytes("$BUILD\KERNEL.BIN")
$kc = [math]::Ceiling($kernel.Length / 512)

fsutil file createnew "$BUILD\floppy.img" 1474560 > $null
$fs = [System.IO.File]::Open("$BUILD\floppy.img", [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write)
$fs.Write($boot, 0, $boot.Length)

$FAT_SIZE = 12 * 512
$SECTORS_PER_FAT = 12
$RESERVED_SECTORS = 1
$NUM_FATS = 2
$ROOT_ENTRIES = 224
$ROOT_SECTORS = [math]::Ceiling($ROOT_ENTRIES * 32 / 512)
$root_lba = $RESERVED_SECTORS + ($NUM_FATS * $SECTORS_PER_FAT)
$data_lba = $root_lba + $ROOT_SECTORS

$fat = [byte[]]::new($FAT_SIZE)
$b0 = [System.BitConverter]::GetBytes([uint16]0xFFF8)
$fat[0] = $b0[0]; $fat[1] = $b0[1]
$b1 = [System.BitConverter]::GetBytes([uint16]0xFFFF)
$fat[2] = $b1[0]; $fat[3] = $b1[1]
for ($i = 0; $i -lt $kc; $i++) {
    $c = 2 + $i
    $next = 0xFFF8
    if ($i -lt $kc - 1) { $next = $c + 1 }
    $off = $c * 2
    $b = [System.BitConverter]::GetBytes([uint16]$next)
    $fat[$off] = $b[0]; $fat[$off + 1] = $b[1]
}
$null = $fs.Seek($RESERVED_SECTORS * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($fat, 0, $fat.Length)
$null = $fs.Seek(($RESERVED_SECTORS + $SECTORS_PER_FAT) * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($fat, 0, $fat.Length)

$entry = [byte[]]::new(32)
$fn = [byte[]]@(0x4B,0x45,0x52,0x4E,0x45,0x4C,0x20,0x20,0x42,0x49,0x4E)
[array]::Copy($fn, 0, $entry, 0, $fn.Length)
$entry[11] = 0x20
$entry[26] = 2; $entry[27] = 0
for ($b = 0; $b -lt 4; $b++) { $entry[28 + $b] = ($kernel.Length -shr ($b * 8)) -band 0xFF }
$null = $fs.Seek($root_lba * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($entry, 0, $entry.Length)

$null = $fs.Seek($data_lba * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($kernel, 0, $kernel.Length)
$fs.Close()

Write-Host "[6/6] Creating FAT16 data disk (hdd.img)..."
$HDD = "$BUILD\hdd.img"
$HDD_SECTORS = 32768
$HDD_SPF = 128
$HDD_ROOT = 512

if (Test-Path $HDD) {
    Write-Host "  hdd.img exists, keeping existing filesystem (delete to reformat)"
} else {
    $bps = 512
    $spc = 1
    $reserved = 1
    $fatCount = 2
    $rootEntries = $HDD_ROOT
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $spf = $HDD_SPF
    $totalSectors = $HDD_SECTORS
    $media = 0xF8

    $bpb = [byte[]]::new(512)
    $bpb[0] = 0xEB; $bpb[1] = 0x3C; $bpb[2] = 0x90
    $oem = [System.Text.Encoding]::ASCII.GetBytes("LUMINAHD ")
    [array]::Copy($oem, 0, $bpb, 3, 8)
    $bpb[11] = $bps -band 0xFF;        $bpb[12] = ($bps -shr 8) -band 0xFF
    $bpb[13] = $spc
    $bpb[14] = $reserved -band 0xFF;   $bpb[15] = ($reserved -shr 8) -band 0xFF
    $bpb[16] = $fatCount
    $bpb[17] = $rootEntries -band 0xFF;$bpb[18] = ($rootEntries -shr 8) -band 0xFF
    $bpb[19] = $totalSectors -band 0xFF;$bpb[20] = ($totalSectors -shr 8) -band 0xFF
    $bpb[21] = $media
    $bpb[22] = $spf -band 0xFF;        $bpb[23] = ($spf -shr 8) -band 0xFF
    $bpb[24] = 63; $bpb[25] = 0
    $bpb[26] = 255; $bpb[27] = 0
    $bpb[510] = 0x55; $bpb[511] = 0xAA

    $fatSize = $spf * $bps
    $fat = [byte[]]::new($fatSize)
    $b0 = [System.BitConverter]::GetBytes([uint16]0xFFF8); $fat[0] = $b0[0]; $fat[1] = $b0[1]
    $b1 = [System.BitConverter]::GetBytes([uint16]0xFFFF); $fat[2] = $b1[0]; $fat[3] = $b1[1]

    $dataLba = $reserved + ($fatCount * $spf) + $rootSectors

    $img = [System.IO.File]::Create($HDD)
    $img.Write($bpb, 0, 512)
    $null = $img.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $img.Write($fat, 0, $fatSize)
    $null = $img.Seek(($reserved + $spf) * $bps, [System.IO.SeekOrigin]::Begin)
    $img.Write($fat, 0, $fatSize)
    $img.SetLength($totalSectors * $bps)
    $img.Close()
    Write-Host "  hdd.img ready (data_lba=$dataLba, $($HDD_SECTORS*$bps) bytes)"
}

Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "  Bootloader:  $($boot.Length) B"
Write-Host "  Kernel:      $kernSz B ($kc clusters)"
Write-Host "  Boot floppy: $BUILD\floppy.img (FAT16, KERNEL.BIN)"
Write-Host "  Data disk:   $BUILD\hdd.img (FAT16, 16 MiB, persistent)"
Write-Host "[7/7] Building apps (LSP)..."
function Write-HddFile([string]$Image, [string]$Name, [byte[]]$Data) {
    $bps = 512; $spc = 1; $reserved = 1; $fatCount = 2; $rootEntries = 512
    $spf = 128
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $dataLba = $reserved + ($fatCount * $spf) + $rootSectors
    $fs = [System.IO.File]::Open($Image, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)

    $rootLba = $reserved + ($fatCount * $spf)
    $clusters = [math]::Ceiling($Data.Length / ($bps * $spc))
    if ($clusters -lt 1) { $clusters = 1 }

    $fat = [byte[]]::new($spf * $bps)
    $null = $fs.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $null = $fs.Read($fat, 0, $fat.Length)

    function Get-FatEntry([uint32]$c) { return [uint16]($fat[$c*2] -bor ($fat[$c*2+1] -shl 8)) }
    function Set-FatEntry([uint32]$c, [uint16]$v) { $fat[$c*2] = $v -band 0xFF; $fat[$c*2+1] = ($v -shr 8) -band 0xFF }

    $clusterList = [System.Collections.Generic.List[uint16]]::new()
    $first = 0
    for ($c = 2; $c -lt ($spf * $bps / 2); $c++) {
        if ((Get-FatEntry $c) -eq 0) {
            if ($first -eq 0) { $first = $c }
            $clusterList.Add($c)
            if ($clusterList.Count -ge $clusters) { break }
        }
    }
    if ($clusterList.Count -lt $clusters) { $fs.Close(); throw "hdd.img out of space" }

    for ($i = 0; $i -lt $clusterList.Count; $i++) {
        $next = 0xFFF8
        if ($i -lt $clusterList.Count - 1) { $next = $clusterList[$i + 1] }
        Set-FatEntry $clusterList[$i] $next
    }

    $null = $fs.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($fat, 0, $fat.Length)
    $null = $fs.Seek(($reserved + $spf) * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($fat, 0, $fat.Length)

    for ($i = 0; $i -lt $clusters; $i++) {
        $off = $dataLba + ($clusterList[$i] - 2) * $spc
        $null = $fs.Seek($off * $bps, [System.IO.SeekOrigin]::Begin)
        $len = $bps * $spc
        if ($i -eq $clusters - 1) { $len = $Data.Length - $i * $len }
        if ($len -lt 0) { $len = 0 }
        if ($len -gt 0) { $fs.Write($Data, $i * ($bps * $spc), $len) }
    }

    $entry = [byte[]]::new(32)
    $baseName = $Name.ToUpper()
    $dotIdx = $baseName.IndexOf('.')
    if ($dotIdx -ge 0) {
        $stem = $baseName.Substring(0, $dotIdx)
        $ext = $baseName.Substring($dotIdx + 1)
    } else {
        $stem = $baseName
        $ext = ""
    }
    if ($stem.Length -gt 8) { $stem = $stem.Substring(0, 8) }
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    $fn = [System.Text.Encoding]::ASCII.GetBytes(($stem + "        ").Substring(0, 8))
    $extB = [System.Text.Encoding]::ASCII.GetBytes(($ext + "   ").Substring(0, 3))
    [array]::Copy($fn, 0, $entry, 0, 8)
    [array]::Copy($extB, 0, $entry, 8, 3)
    $entry[11] = 0x20
    $entry[26] = $first -band 0xFF; $entry[27] = ($first -shr 8) -band 0xFF
    $szB = [BitConverter]::GetBytes([uint32]$Data.Length)
    [array]::Copy($szB, 0, $entry, 28, 4)

    $root = [byte[]]::new($rootSectors * $bps)
    $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
    $null = $fs.Read($root, 0, $root.Length)
    $entryName = [System.Text.Encoding]::ASCII.GetString($fn) + [System.Text.Encoding]::ASCII.GetString($extB)
    $slot = -1
    $free = -1
    for ($i = 0; $i -lt $rootEntries; $i++) {
        $isFree = $root[$i * 32] -eq 0
        $isDel = $root[$i * 32] -eq 0xE5
        $cur = [System.Text.Encoding]::ASCII.GetString($root, $i * 32, 11)
        if ($cur -eq $entryName) { $slot = $i; break }
        if ($free -lt 0 -and ($isFree -or $isDel)) { $free = $i }
    }
    if ($slot -lt 0) { $slot = $free }
    if ($slot -ge 0) {
        [array]::Copy($entry, 0, $root, $slot * 32, 32)
    }
    $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($root, 0, $root.Length)
    $fs.Close()
}

function Remove-HddFile([string]$Image, [string]$Name) {
    $bps = 512; $reserved = 1; $fatCount = 2; $rootEntries = 512
    $spf = 128
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $rootLba = $reserved + ($fatCount * $spf)
    if (-not (Test-Path $Image)) { return }
    $fs = [System.IO.File]::Open($Image, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)

    $baseName = $Name.ToUpper()
    $dotIdx = $baseName.IndexOf('.')
    if ($dotIdx -ge 0) {
        $stem = $baseName.Substring(0, $dotIdx)
        $ext = $baseName.Substring($dotIdx + 1)
    } else {
        $stem = $baseName
        $ext = ""
    }
    if ($stem.Length -gt 8) { $stem = $stem.Substring(0, 8) }
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    $fn = [System.Text.Encoding]::ASCII.GetBytes(($stem + "        ").Substring(0, 8))
    $extB = [System.Text.Encoding]::ASCII.GetBytes(($ext + "   ").Substring(0, 3))
    $entryName = [System.Text.Encoding]::ASCII.GetString($fn) + [System.Text.Encoding]::ASCII.GetString($extB)

    $root = [byte[]]::new($rootSectors * $bps)
    $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
    $null = $fs.Read($root, 0, $root.Length)

    $slot = -1
    for ($i = 0; $i -lt $rootEntries; $i++) {
        $cur = [System.Text.Encoding]::ASCII.GetString($root, $i * 32, 11)
        if ($cur -eq $entryName) { $slot = $i; break }
    }
    if ($slot -ge 0) {
        $root[$slot * 32] = 0xE5
        $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
        $fs.Write($root, 0, $root.Length)
        Write-Host "  Removed $Name from hdd.img"
    }
    $fs.Close()
}

$APPS = Get-ChildItem "$SDK\examples\*.c" -ErrorAction SilentlyContinue
$acflags = "-ffreestanding", "-nostdlib", "-fno-pic", "-fno-stack-protector", "-I$SDK\include", "-m32", "-O2", "-c"
Remove-HddFile "$HDD" "DEMO.LSP"
foreach ($app in $APPS) {
    $name = $app.BaseName
    $obj = "$BUILD\$name.o"
    $elf = "$BUILD\$name.elf"
    $bin = "$BUILD\$name.bin"
    $lsp = "$BUILD\$name.lsp"
    i686-elf-gcc @acflags $app.FullName -o $obj
    i686-elf-ld -T "$SDK\app.ld" -o $elf $obj
    i686-elf-objcopy -O binary $elf $bin
    $code = [IO.File]::ReadAllBytes($bin)
    $nm = & i686-elf-nm $elf | Select-String " _start$" | Select-Object -First 1
    $entry = 0x10
    if ($nm -and $nm.ToString() -match '^\s*([0-9a-fA-F]+)') {
        $entry = [Convert]::ToUInt32($Matches[1], 16) - 0x01000000
    }
    $hdr = [byte[]]::new(16)
    $hdr[0] = 0x4C; $hdr[1] = 0x53; $hdr[2] = 0x50; $hdr[3] = 1
    $e = [BitConverter]::GetBytes([uint32]$entry); [Array]::Copy($e, 0, $hdr, 4, 4)
    $l = [BitConverter]::GetBytes([uint32]$code.Length); [Array]::Copy($l, 0, $hdr, 8, 4)
    $b = [BitConverter]::GetBytes([uint32]0); [Array]::Copy($b, 0, $hdr, 12, 4)
    $img = $hdr + $code
    [IO.File]::WriteAllBytes($lsp, $img)
    Write-Host "  $($name).lsp: $($img.Length) B"
    Write-HddFile "$HDD" "$name.LSP" $img
}

Write-Host "[8/8] CJK font..."
$FONT_BIN = "$BUILD\font16.bin"
& "$ROOT\tools\gen_font.ps1" "$FONT_BIN"
if ($?) {
    $fontData = [IO.File]::ReadAllBytes($FONT_BIN)
    Remove-HddFile "$HDD" "FONT16.BIN"
    Write-HddFile "$HDD" "FONT16.BIN" $fontData
    Write-Host "  FONT16.BIN: $($fontData.Length) B"
}
Write-Host "=== Build complete ===" -ForegroundColor Green
