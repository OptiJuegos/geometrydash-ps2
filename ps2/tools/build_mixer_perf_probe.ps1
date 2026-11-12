param([string]$ToolchainRoot = '')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$sdk = Join-Path $ToolchainRoot 'ps2sdk'
$cc = Join-Path $ToolchainRoot 'ee/bin/mips64r5900el-ps2-elf-gcc.exe'
$env:PATH = (Join-Path $ToolchainRoot 'ee/bin') + ';' + $env:PATH
$include = @("-I$(Join-Path $sdk 'ee/include')",
             "-I$(Join-Path $sdk 'common/include')",
             "-I$(Join-Path $root 'psp/include')",
             "-I$(Join-Path $root 'ps2/src')")
foreach ($flavor in @('reference','float','fixed')) {
    $flag = switch ($flavor) {
        'reference' { '-DPS2_MIXER_DISABLE_FAST_STEREO' }
        'fixed' { '-DPS2_MIXER_FAST_FIXED' }
        default { '-DPS2_MIXER_FAST_FLOAT' }
    }
    $out = Join-Path $root "ps2/out/gdps2-mixer-$flavor-probe.elf"
    $args_cc = @('-D_EE','-D__PS2__','-G0','-O2','-std=gnu11',
        '-Wall','-Wextra',$flag) + $include +
        @("-T$(Join-Path $sdk 'ee/startup/linkfile')",
          '-Wl,-zmax-page-size=128',"-L$(Join-Path $sdk 'ee/lib')",
          (Join-Path $root 'ps2/tests/mixer_perf_ps2_probe.c'),
          (Join-Path $root 'ps2/src/ndsp_mixer.c'),'-o',$out,
          '-lm','-lc','-lkernel')
    & $cc @args_cc
    if ($LASTEXITCODE -ne 0) { throw "Mixer $flavor probe failed to compile" }
    Write-Host "Mixer $flavor probe: $out"
}
