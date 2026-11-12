param([string]$HostCC = "gcc")

$ErrorActionPreference = "Stop"
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$out = Join-Path $root "ps2/out/tests"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out "ps2_fast_float_smoke.exe"
$ccArgs = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-D__PS2__",
            (Join-Path $root "ps2/tests/ps2_fast_float_smoke.c"), "-lm", "-o", $exe)
& $HostCC @ccArgs
if ($LASTEXITCODE -ne 0) { throw "PS2 float parser host compilation failed" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "PS2 float parser smoke failed" }
