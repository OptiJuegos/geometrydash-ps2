param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'mp3_decode_flow_smoke.exe'
$args_cc = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'ps2/src')",
    "-I$(Join-Path $root 'psdevwindows/ps2sdk/ports/include')",
    (Join-Path $root 'ps2/tests/mp3_decode_flow_smoke.c'), '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "MP3 flow host test compile failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "MP3 flow host test failed: $LASTEXITCODE" }
