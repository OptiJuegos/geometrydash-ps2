param(
    [string]$PCSX2 = ''
)

# Open the ORIGINAL Geometry Dash engine, never the standalone square prototype.
# This uses the normal PCSX2 configuration already installed by the user.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$elf = Join-Path $root 'ps2/out/gdps2-engine-experimental.elf'
$package = Join-Path $root 'ps2/out/package/romfs'

foreach ($file in @($elf, (Join-Path $package 'menus/main_menu.txt'),
                      (Join-Path $package 'gfx/ui.p2sheet'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Falta el motor o sus recursos: $file. Ejecutar build_engine.ps1 -PackageRomfs primero."
    }
}

if (-not $PCSX2 -and $env:PCSX2_PATH) { $PCSX2 = $env:PCSX2_PATH }
if (-not $PCSX2) {
    foreach ($name in @('pcsx2-qt.exe', 'pcsx2.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { $PCSX2 = $command.Source; break }
    }
}
if (-not $PCSX2) {
    foreach ($candidate in @(
        'D:\Programas\pcsx2-sanity-v1.0.3-windows-x64-Qt\pcsx2-qt.exe',
        'C:\Program Files\PCSX2\pcsx2-qt.exe',
        'C:\Program Files (x86)\PCSX2\pcsx2-qt.exe')) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $PCSX2 = $candidate
            break
        }
    }
}
if (-not $PCSX2 -or -not (Test-Path -LiteralPath $PCSX2 -PathType Leaf)) {
    throw 'PCSX2 no localizado. Pasar -PCSX2 "ruta\pcsx2-qt.exe".'
}

$PCSX2 = [IO.Path]::GetFullPath($PCSX2)
Write-Host "Motor original: $elf"
Write-Host "Recursos ROMFS: $package"
Write-Host 'Es necesario tener HostFs habilitado en PCSX2 para leer host:package/romfs/.'
Write-Host 'El motor original arranca pero los graficos GS aun tienen corrupcion.'
Start-Process -FilePath $PCSX2 -ArgumentList @('-elf', ('"' + $elf + '"')) `
    -WorkingDirectory (Split-Path -Parent $PCSX2)
