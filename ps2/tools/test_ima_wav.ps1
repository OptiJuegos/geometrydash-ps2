param([string]$HostCC = 'gcc', [string]$FFmpeg = 'ffmpeg')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'ima_wav_smoke.exe'
$wav = Join-Path $out 'ima_wav_fixture.wav'
$raw = Join-Path $out 'ima_wav_fixture.s16le'
$mp3 = Join-Path $root 'romfs/songs/menuLoop.mp3'

& $FFmpeg -v error -y -i $mp3 -ar 32000 -ac 2 -c:a adpcm_ima_wav $wav
if ($LASTEXITCODE -ne 0) { throw "ffmpeg encode failed: $LASTEXITCODE" }
& $FFmpeg -v error -y -i $wav -f s16le -c:a pcm_s16le $raw
if ($LASTEXITCODE -ne 0) { throw "ffmpeg reference decode failed: $LASTEXITCODE" }

$args_cc = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/ima_wav_smoke.c'), '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "IMA WAV host test compile failed: $LASTEXITCODE" }
& $exe $wav $raw
if ($LASTEXITCODE -ne 0) { throw "IMA WAV host test failed: $LASTEXITCODE" }
