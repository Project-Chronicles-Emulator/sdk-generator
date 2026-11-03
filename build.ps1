#requires -Version 7.2
param([string]$PlatformToolset = 'v143')

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
    -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw 'Install Visual Studio C++ tools and MSBuild first.'
}

$solution = Join-Path $PSScriptRoot 'ArcheAgeSdkGenerator.sln'
& $msbuild $solution /m /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset"
if ($LASTEXITCODE -ne 0) {
    throw "SDK generator build failed with exit code $LASTEXITCODE."
}
