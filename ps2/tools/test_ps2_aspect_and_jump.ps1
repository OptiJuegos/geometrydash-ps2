param([string]$HostCC = 'gcc')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$aspectExe = Join-Path $out 'aspect_4_3_smoke.exe'
& $HostCC -std=c11 -Wall -Wextra -Werror (Join-Path $root 'ps2/tests/aspect_4_3_smoke.c') -o $aspectExe
if ($LASTEXITCODE -ne 0) { throw 'Aspect smoke compile failed' }
& $aspectExe
if ($LASTEXITCODE -ne 0) { throw 'Aspect smoke failed' }

$jumpExe = Join-Path $out 'input_release_smoke.exe'
& $HostCC -std=c11 -Wall -Wextra -Werror "-I$(Join-Path $root 'ps2/tests/input_fake')" (Join-Path $root 'ps2/tests/input_release_smoke.c') -o $jumpExe
if ($LASTEXITCODE -ne 0) { throw 'Input release smoke compile failed' }
& $jumpExe
if ($LASTEXITCODE -ne 0) { throw 'Input release smoke failed' }

$padExe = Join-Path $out 'pad_decode_jump_smoke.exe'
& $HostCC -std=c11 -Wall -Wextra -Werror "-I$(Join-Path $root 'ps2/tests/input_fake')" "-I$(Join-Path $root 'psdevwindows/ps2sdk/ee/include')" (Join-Path $root 'ps2/tests/pad_decode_jump_smoke.c') -o $padExe
if ($LASTEXITCODE -ne 0) { throw 'PS2 libpad decoding smoke compile failed' }
& $padExe
if ($LASTEXITCODE -ne 0) { throw 'PS2 libpad decoding smoke failed' }

$navExe = Join-Path $out 'nav_hover_smoke.exe'
& $HostCC -std=c11 -Wall -Wextra -Werror "-I$(Join-Path $root 'ps2/tests/input_fake')" (Join-Path $root 'ps2/tests/nav_hover_smoke.c') -o $navExe
if ($LASTEXITCODE -ne 0) { throw 'PS2 hover navigation smoke compile failed' }
& $navExe
if ($LASTEXITCODE -ne 0) { throw 'PS2 hover navigation smoke failed' }
