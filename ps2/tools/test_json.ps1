param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'json_compat_smoke.exe'
$header = Join-Path $root 'psp/include'
$thirdParty = Join-Path $root 'ps2/third_party'
$test = Join-Path $root 'ps2/tests/json_compat_smoke.c'
$cjson = Join-Path $thirdParty 'cjson/cJSON.c'
& $HostCC '-std=c11' '-Wall' '-Wextra' "-I$header" "-I$thirdParty" $test $cjson '-lm' '-o' $exe
if ($LASTEXITCODE -ne 0) { throw "Host JSON smoke build failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Host JSON smoke run failed: $LASTEXITCODE" }
