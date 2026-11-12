param(
    [string]$Elf = 'ps2\out\gdps2-diag-vblank-ft.elf',
    [int]$Seconds = 110,
    [string]$PCSX2 = 'D:\Programas\pcsx2-sanity-v1.0.3-windows-x64-Qt\pcsx2-qt.exe'
)
# Runs a -DiagnosticFrameTiming ELF in the ISOLATED sanity PCSX2 (its own data
# dir under ps2/out/pcsx2-smoke), then prints the GDPS2 FT lines from ps2/out/ft.log.
# Only the process started here is stopped; any other PCSX2 stays untouched.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$elfPath = [IO.Path]::GetFullPath((Join-Path $root $Elf))
$log = Join-Path (Split-Path $elfPath) 'ft.log'
Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $PCSX2 -ArgumentList '-elf', ('"' + $elfPath + '"') `
    -WorkingDirectory (Join-Path $root 'ps2\out\pcsx2-smoke') -PassThru
Start-Sleep $Seconds
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Select-String -Path $log -Pattern 'GDPS2 FT|GDPS2 AUDIO GAP' | ForEach-Object { $_.Line }
