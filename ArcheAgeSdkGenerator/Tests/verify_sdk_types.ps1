param(
	[Parameter(Mandatory = $true)][string]$Dump,
	[string]$Compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
)

$ErrorActionPreference = 'Stop'
$dumpPath = (Resolve-Path -LiteralPath $Dump).Path
$output = Join-Path $PSScriptRoot '../../x64/Tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$kit = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
	Where-Object { Test-Path (Join-Path $_.FullName 'ucrt/crtdbg.h') } |
	Sort-Object Name -Descending | Select-Object -First 1
if (!$kit) { throw 'Windows SDK UCRT headers were not found.' }
$libraries = "C:/Program Files (x86)/Windows Kits/10/Lib/$($kit.Name)"
$flags = @('-std=c++20', '-UNDEBUG', '-isystem', "$($kit.FullName)/ucrt",
	'-L', "$libraries/ucrt/x64", '-L', "$libraries/um/x64")

foreach ($name in @('verify_enum_array', 'verify_sdk_layout'))
{
	$executable = Join-Path $output "$name.exe"
	& $Compiler @flags -I "$dumpPath/SDK/SDK" "$PSScriptRoot/$name.cpp" -o $executable
	if ($LASTEXITCODE) { throw "$name compilation failed." }
	& $executable
	if ($LASTEXITCODE) { throw "$name failed." }
}

python "$PSScriptRoot/verify_sdk_types.py" $dumpPath
if ($LASTEXITCODE) { throw 'Generated SDK type audit failed.' }
