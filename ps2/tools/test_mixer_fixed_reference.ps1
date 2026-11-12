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
$fixedExe = Join-Path $out 'mixer_fixed.exe'
$refExe = Join-Path $out 'mixer_reference.exe'
& $HostCC '-DPS2_MIXER_FAST_FIXED' @common '-o' $fixedExe
if ($LASTEXITCODE -ne 0) { throw 'Fixed mixer compilation failed' }
& $HostCC '-DPS2_MIXER_DISABLE_FAST_STEREO' @common '-o' $refExe
if ($LASTEXITCODE -ne 0) { throw 'Reference mixer compilation failed' }
$fixed = @(& $fixedExe)
if ($LASTEXITCODE -ne 0) { throw 'Fixed mixer failed' }
$reference = @(& $refExe)
if ($LASTEXITCODE -ne 0) { throw 'Reference mixer failed' }
if ($fixed.Count -ne $reference.Count) { throw 'Different reference scenario counts' }
$maxError = 0; $sumError = 0.0; $samples = 0
for ($i=0; $i -lt $fixed.Count; ++$i) {
    $f = $fixed[$i] -split 'status ', 2
    $r = $reference[$i] -split 'status ', 2
    if ($f.Count -ne 2 -or $r.Count -ne 2) { throw "Malformed scenario $i" }
    $fs = @(($f[1] -split '\s+') | Where-Object { $_ })
    $rs = @(($r[1] -split '\s+') | Where-Object { $_ })
    if ($fs.Count -ne $rs.Count -or $fs.Count -ne 7) { throw "Malformed statuses $i" }
    foreach ($k in @(0,1,2,3,5,6)) {
        if ($fs[$k] -ne $rs[$k]) { throw "Queue mismatch scenario $i field $k" }
    }
    if ([math]::Abs([double]::Parse($fs[4],[Globalization.CultureInfo]::InvariantCulture) -
                    [double]::Parse($rs[4],[Globalization.CultureInfo]::InvariantCulture)) -gt 0.02) {
        throw "Phase drift scenario $i exceeds 0.02 frame"
    }
    $fa = @(($f[0] -split '\s+') | Where-Object { $_ })
    $ra = @(($r[0] -split '\s+') | Where-Object { $_ })
    if ($fa.Count -ne $ra.Count) { throw "Output length mismatch scenario $i" }
    for ($n=0; $n -lt $fa.Count; ++$n) {
        $diff = [math]::Abs([int]$fa[$n] - [int]$ra[$n]);
        if ($diff -gt $maxError) { $maxError = $diff }
        $sumError += $diff; ++$samples
    }
}
$mean = $sumError / $samples
Write-Host ("Fixed mixer error vs reference: max={0} PCM16 units, mean={1:N2}, samples={2}" -f $maxError,$mean,$samples)
if ($maxError -gt 64 -or $mean -gt 15) { throw 'Fixed mixer exceeds reference tolerance' }
Write-Host 'PASS: fixed-point mixer PCM16 output and buffer/phase state within tolerance'
