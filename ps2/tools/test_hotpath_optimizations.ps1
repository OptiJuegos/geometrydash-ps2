param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sortExe = Join-Path $out 'sprite_sort_small_smoke.exe'
& $HostCC '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' (Join-Path $root 'ps2/tests/sprite_sort_small_smoke.c') '-o' $sortExe
if ($LASTEXITCODE -ne 0) { throw 'Sprite sort smoke build failed' }
& $sortExe
if ($LASTEXITCODE -ne 0) { throw 'Sprite sort mismatch' }

$splitExe = Join-Path $out 'level_split_smoke.exe'
& $HostCC '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' (Join-Path $root 'ps2/tests/level_split_smoke.c') '-o' $splitExe
if ($LASTEXITCODE -ne 0) { throw 'Level tokenizer smoke build failed' }
& $splitExe
if ($LASTEXITCODE -ne 0) { throw 'Level tokenizer changed parsing semantics' }

$src = Join-Path $root 'ps2/tests/mixer_fast_reference.c'
$mixer = Join-Path $root 'ps2/src/ndsp_mixer.c'
$common = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-DPS2_MIXER_DISABLE_FAST_STEREO',
    "-I$(Join-Path $root 'psp/include')",
    "-I$(Join-Path $root 'ps2/src')", $src, $mixer, '-lm')
$oldExe = Join-Path $out 'mixer_step_uncached.exe'
$newExe = Join-Path $out 'mixer_step_cached.exe'
& $HostCC @common '-o' $oldExe
if ($LASTEXITCODE -ne 0) { throw 'Original mixer build failed' }
& $HostCC '-DPS2_MIXER_ENABLE_STEP_CACHE' @common '-o' $newExe
if ($LASTEXITCODE -ne 0) { throw 'Cached mixer build failed' }
$old = (& $oldExe) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0) { throw 'Original mixer run failed' }
$new = (& $newExe) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0) { throw 'Cached mixer run failed' }
if ($old -cne $new) { throw 'Mixer sample, playback phase or buffer state changed' }
Write-Host 'PASS: mixer cached-step PCM samples, queue status and final phase match reference exactly'
