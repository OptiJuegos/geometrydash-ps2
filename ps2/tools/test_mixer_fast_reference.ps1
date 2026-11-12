param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$src = Join-Path $root 'ps2/tests/mixer_fast_reference.c'
$mixer = Join-Path $root 'ps2/src/ndsp_mixer.c'
$common = @('-std=c11','-O2','-Wall','-Wextra','-Werror',
    "-I$(Join-Path $root 'psp/include')", "-I$(Join-Path $root 'ps2/src')",
    $src, $mixer, '-lm')
$fast = Join-Path $out 'mixer_fast.exe'
$slow = Join-Path $out 'mixer_reference.exe'
& $HostCC @common '-o' $fast
if ($LASTEXITCODE -ne 0) { throw 'Fast mixer test compile failed' }
& $HostCC '-DPS2_MIXER_DISABLE_FAST_STEREO' @common '-o' $slow
if ($LASTEXITCODE -ne 0) { throw 'Reference mixer test compile failed' }
$fastOutput = (& $fast | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'Fast mixer test exited nonzero' }
$slowOutput = (& $slow | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'Reference mixer test exited nonzero' }
if ($fastOutput -cne $slowOutput) {
    $fastOutput | Set-Content -LiteralPath (Join-Path $out 'mixer_fast_result.txt')
    $slowOutput | Set-Content -LiteralPath (Join-Path $out 'mixer_reference_result.txt')
    throw 'Fast stereo PCM16 mixer does not match generic reference output/state'
}
Write-Host 'PASS: fast stereo PCM16 matches generic reference across 6 cases (phase, queues, looping, SFX)'
