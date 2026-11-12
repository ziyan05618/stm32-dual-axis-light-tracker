$ErrorActionPreference='Stop'
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
$cc=Join-Path $here 'tools\arm-gnu\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin'
if($env:ARM_GNU_BIN) { $cc=$env:ARM_GNU_BIN }
elseif(-not (Test-Path -LiteralPath (Join-Path $cc 'arm-none-eabi-gcc.exe'))) {
 $installed=Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
 if(-not $installed) { throw 'GNU Arm compiler not found. Set ARM_GNU_BIN or install the toolchain documented in README.md.' }
 $cc=Split-Path -Parent $installed.Source
}
$gcc=Join-Path $cc 'arm-none-eabi-gcc.exe'
$objcopy=Join-Path $cc 'arm-none-eabi-objcopy.exe'
$size=Join-Path $cc 'arm-none-eabi-size.exe'
Push-Location $here
try {
 New-Item -ItemType Directory -Force build | Out-Null
 $ccArgs=@('-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-Os','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-std=c11','-ffreestanding','-fno-builtin','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-Isrc','src/startup.S','src/main.c','src/board.c','src/control.c','src/position_store.c','src/display.c','src/ui.c','-nostdlib','-Wl,--gc-sections','-Wl,-Map=build/tracker.map','-Tsrc/stm32f103c8.ld','-lgcc','-o','build/tracker.elf')
 & $gcc @ccArgs
 if($LASTEXITCODE -ne 0){throw 'Firmware compile/link failed.'}
 & $objcopy -O ihex build/tracker.elf build/tracker.hex
 if($LASTEXITCODE -ne 0){throw 'HEX conversion failed.'}
 & $objcopy -O binary build/tracker.elf build/tracker.bin
 if($LASTEXITCODE -ne 0){throw 'BIN conversion failed.'}
 & $size build/tracker.elf
 if($LASTEXITCODE -ne 0){throw 'ELF size inspection failed.'}
 Write-Host 'Built tracker.elf, tracker.hex and tracker.bin in build.'
} finally { Pop-Location }
