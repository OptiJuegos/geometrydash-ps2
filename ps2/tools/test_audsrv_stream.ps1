param([string]$HostCC = 'gcc')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'audsrv_stream_smoke.exe'
& $HostCC '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' "-I$(Join-Path $root 'ps2/src')" `
    (Join-Path $root 'ps2/tests/audsrv_stream_smoke.c') `
    (Join-Path $root 'ps2/src/audsrv_stream.c') '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'audsrv submit smoke compile failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'audsrv submit smoke failed' }
