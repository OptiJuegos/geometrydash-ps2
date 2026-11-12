param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'swkbd_edit_smoke.exe'
$args_cc = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'psp/include')",
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/swkbd_edit_smoke.c'),
    (Join-Path $root 'ps2/src/swkbd_edit.c'),
    '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "PS2 keyboard host compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "PS2 keyboard host test failed: $LASTEXITCODE" }
