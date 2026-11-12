param([string]$HostCC = 'gcc')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'save_selection_smoke.exe'
$args_cc = @('-std=c11', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'ps2/tests/save_fake')",
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/save_selection_smoke.c'),
    (Join-Path $root 'ps2/src/romfs_paths.c'),
    (Join-Path $root 'ps2/src/asset_locator.c'),
    (Join-Path $root 'ps2/src/disc_names.c'),
    (Join-Path $root 'ps2/src/romfs_ps2.c'), '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "Save selection smoke build failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Save selection smoke failed: $LASTEXITCODE" }
