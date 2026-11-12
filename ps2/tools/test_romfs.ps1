param([string]$HostCC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'ps2/out/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'romfs_paths_smoke.exe'
$args_cc = @('-std=c11', '-Wall', '-Wextra', '-Werror',
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/romfs_paths_smoke.c'),
    (Join-Path $root 'ps2/src/romfs_paths.c'), '-o', $exe)
& $HostCC @args_cc
if ($LASTEXITCODE -ne 0) { throw "Host ROMFS test build failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Host ROMFS test failed: $LASTEXITCODE" }

$assetExe = Join-Path $out 'asset_locator_smoke.exe'
$assetArgs = @('-std=c11','-Wall','-Wextra','-Werror',
    "-I$(Join-Path $root 'ps2/src')",
    (Join-Path $root 'ps2/tests/asset_locator_smoke.c'),
    (Join-Path $root 'ps2/src/asset_locator.c'), '-o', $assetExe)
& $HostCC @assetArgs
if ($LASTEXITCODE -ne 0) { throw "Asset locator test compilation failed: $LASTEXITCODE" }
& $assetExe
if ($LASTEXITCODE -ne 0) { throw "Asset locator test failed: $LASTEXITCODE" }

# MinGW does not provide newlib's fopencookie, so the host harness invokes
# the real MCPK mount/read/seek callbacks directly with concurrent streams.
# Uses the generated package as the authoritative fixture.
$pak = Join-Path $root 'ps2/out/package/assets.pak'
if (Test-Path -LiteralPath $pak) {
    $pakExe = Join-Path $out 'asset_pak_posix_smoke.exe'
    $pakArgs = @('-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
        "-I$(Join-Path $root 'ps2/src')",
        (Join-Path $root 'ps2/tests/asset_pak_posix_smoke.c'), '-o', $pakExe)
    & $HostCC @pakArgs
    if ($LASTEXITCODE -ne 0) { throw "POSIX pak test compilation failed: $LASTEXITCODE" }
    & $pakExe $pak
    if ($LASTEXITCODE -ne 0) { throw "POSIX shared-descriptor pak test failed: $LASTEXITCODE" }
} else {
    Write-Warning 'Generate assets.pak to run the optional shared-descriptor smoke test.'
}
