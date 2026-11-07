[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Invoke-OpenOcd {
 param([string[]]$ToolArguments, [string]$LogPath, [switch]$Append)
 # OpenOCD writes normal status messages to stderr. Windows PowerShell 5.1
 # wraps these in NativeCommandError records; do not treat them as failure.
 # Preserve the log and use the process exit code to detect real failures.
 $savedPreference = $ErrorActionPreference
 $nativeExitCode = $null
 try {
  if(-not $Append) { Set-Content -LiteralPath $LogPath -Value '' -NoNewline -Encoding UTF8 -ErrorAction Stop }
  $ErrorActionPreference = 'Continue'
  & $openocd @ToolArguments 2>&1 | ForEach-Object {
   $line = $_.ToString()
   Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 -ErrorAction Stop
   Write-Output $line
  }
  $nativeExitCode = $LASTEXITCODE
 } finally {
  $ErrorActionPreference = $savedPreference
 }
 if($nativeExitCode -ne 0) { throw ('OpenOCD failed (exit code '+$nativeExitCode+'). See '+$LogPath) }
}

Push-Location $here
try {
 $openocd = Join-Path $here 'tools/openocd/xpack-openocd-0.12.0-7/bin/openocd.exe'
 if($env:OPENOCD_EXE) { $openocd=$env:OPENOCD_EXE }
 elseif(-not (Test-Path -LiteralPath $openocd)) {
  $installed=Get-Command openocd.exe -ErrorAction SilentlyContinue
  if(-not $installed) { throw 'OpenOCD not found. Install it or set OPENOCD_EXE; see README.md.' }
  $openocd=$installed.Source
 }
 $firmwareDirectory=if((Test-Path -LiteralPath 'build/tracker.elf') -and (Test-Path -LiteralPath 'build/tracker.bin')){'build'}else{'prebuilt'}
 $elf=$firmwareDirectory+'/tracker.elf'
 $binary=$firmwareDirectory+'/tracker.bin'
 foreach($file in @($openocd, $elf, $binary, 'tools/stlink-target.cfg')) {
  if(-not (Test-Path -LiteralPath $file)) { throw ('Missing file: '+$file) }
 }
 $length = (Get-Item -LiteralPath $binary).Length
 if($length -lt 8 -or $length -gt 65536) { throw 'Firmware size is outside the STM32F103C8 Flash range.' }
 New-Item -ItemType Directory -Force backups,evidence,build | Out-Null
 $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
 $backup = 'backups/before-flash-'+$stamp+'-64KiB.bin'
 $options = 'backups/options-'+$stamp+'.bin'
 $log = 'evidence/flash-'+$stamp+'.log'
 $checkChip = 'if {([lindex [read_memory 0xe0042000 32 1] 0] & 0xfff) != 0x410} {error {Unexpected target device; refusing to program}}'
 $checkSize = 'if {[lindex [read_memory 0x1ffff7e0 16 1] 0] != 64} {error {Expected 64 KiB Flash; refusing to program}}'
 $checkRdp = 'if {([lindex [read_memory 0x4002201c 32 1] 0] & 2) != 0} {error {Read protection active; refusing to unlock or erase}}'
 $probeArgs = @('-f','tools/stlink-target.cfg','-c','init','-c','reset halt','-c','mmw 0xe0042004 0x300 0','-c',$checkChip,'-c',$checkSize,'-c',$checkRdp,'-c',('dump_image '+$backup+' 0x08000000 65536'),'-c',('dump_image '+$options+' 0x1ffff800 16'),'-c',('verify_image '+$backup+' 0x08000000 bin'),'-c','resume','-c','shutdown')
 Invoke-OpenOcd -ToolArguments $probeArgs -LogPath $log
 if((Get-Item -LiteralPath $backup).Length -ne 65536) { throw 'Incomplete backup. Refusing to program.' }
 Get-FileHash -LiteralPath $backup,$options -Algorithm SHA256 | Format-List | Out-String | Add-Content 'backups/SHA256.txt'
 $programArgs = @('-f','tools/stlink-target.cfg','-c',('program '+$elf+' verify'),'-c',('dump_image build/tracker-readback.bin 0x08000000 '+$length),'-c','reset run','-c','shutdown')
 Invoke-OpenOcd -ToolArguments $programArgs -LogPath $log -Append
 $expected = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
 $actual = (Get-FileHash 'build/tracker-readback.bin' -Algorithm SHA256).Hash
 if($expected -ne $actual) { throw 'Flash readback SHA-256 mismatch.' }
 $confirmation = 'READBACK_SHA256_MATCH='+$actual
 Add-Content -LiteralPath $log -Value $confirmation -Encoding UTF8
 Write-Output $confirmation
 Write-Host ('Flash verified and running. Backup: '+$backup)
} finally {
 Pop-Location
}
