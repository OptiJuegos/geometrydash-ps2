param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'ndsp_mixer_smoke.exe'
$args_cc = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'psp/include')",
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/ndsp_mixer_smoke.c'),
    (Join-Path $root 'ps2/src/ndsp_mixer.c'),
    '-lm', '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "NDSP host test failed compilation: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "NDSP host test failed: $LASTEXITCODE" }
