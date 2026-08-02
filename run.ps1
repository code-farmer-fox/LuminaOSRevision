param(
    [switch]$NoDebug,
    [switch]$Serial,
    [switch]$Headless
)

$img = "$PSScriptRoot\build\floppy.img"
$hdd = "$PSScriptRoot\build\hdd.img"
if (-not (Test-Path $img)) {
    Write-Host "Error: $img not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $hdd)) {
    Write-Host "Error: $hdd not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

$qemu_args = @(
    "-drive", "file=$img,format=raw,if=floppy",
    "-drive", "file=$hdd,format=raw,if=ide,media=disk",
    "-boot", "a",
    "-m", "32M",
    "-rtc", "base=localtime"
)

if ($Headless) {
    $qemu_args += "-nographic"
    $qemu_args += "-debugcon", "file:$PSScriptRoot\build\debug.log"
} else {
    $qemu_args += "-display", "sdl"
}

if ($Serial) {
    $qemu_args += "-serial", "file:$PSScriptRoot\build\serial.log"
}

if (-not $NoDebug) {
    $qemu_args += "-gdb", "tcp::1234"
}

Write-Host "Starting LuminaOS in QEMU..." -ForegroundColor Cyan
& qemu-system-i386 @qemu_args
