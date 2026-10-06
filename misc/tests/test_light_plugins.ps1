# Thornswood #1715. Builds misc\tests\test_light_plugins.cpp with MSVC from
# the libespm and FormDesc sources of a checkout and runs it against a game's
# Data folder and the plugin lists one of its sessions wrote.
#
#   -Source  the checkout whose libespm and FormDesc are under test (this one
#            by default). The test builds against the code before the change
#            too, so the same run on an older checkout shows what was broken.
#   -Build   a configured engine build folder: its vcpkg fmt, spdlog and zlib are
#            used, static with the static runtime as the x64-windows-sp triplet
#            builds them.
#   -Data, -Plugins, -Ccc  the game's Data folder, plugins.txt and Skyrim.ccc.
#   -ClientFull, -ClientLight  the session's Game.getModName and
#            Game.getLightModName lists (thornswood-front.js writes them to
#            Data\Platform\thornswood-loadorder.txt and
#            thornswood-plugins-light.txt).
# Prints each check and exits 1 when one fails. Leaves nothing behind.
param(
  [string]$Source = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))),
  [string]$Build = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))) 'build'),
  [string]$Data = 'C:\Thornswood\Game\Data',
  [string]$Plugins = 'C:\Thornswood\Profile\LocalAppData\Plugins.txt',
  [string]$Ccc = 'C:\Thornswood\Game\Skyrim.ccc',
  [string]$ClientFull = 'C:\Thornswood\Game\Data\Platform\thornswood-loadorder.txt',
  [string]$ClientLight = 'C:\Thornswood\Game\Data\Platform\thornswood-plugins-light.txt'
)
$ErrorActionPreference = 'Stop'
$test = Join-Path (Split-Path -Parent $PSCommandPath) 'test_light_plugins.cpp'
foreach ($p in $Data, $Plugins, $Ccc, $ClientFull, $ClientLight, $test) {
  if (-not (Test-Path -LiteralPath $p)) { throw "Missing $p" }
}
$include = Join-Path $Build 'vcpkg_installed\x64-windows-sp\include'
$lib = Join-Path $Build 'vcpkg_installed\x64-windows-sp\lib'
if (-not (Test-Path -LiteralPath (Join-Path $include 'fmt\format.h'))) {
  throw "No vcpkg fmt in $Build; pass -Build with a configured engine build folder"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ tools' }
foreach ($line in (cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set")) {
  if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('thornswood-light-plugins-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
  $sources = @(Get-ChildItem -LiteralPath (Join-Path $Source 'libespm\src') -Filter *.cpp | ForEach-Object FullName)
  $sources += Join-Path $Source 'viet\src\AllocatedBuffer.cpp'
  $sources += Join-Path $Source 'viet\src\MappedBuffer.cpp'
  $sources += Join-Path $Source 'skymp5-server\cpp\server_guest_lib\FormDesc.cpp'
  $libs = @('fmt.lib', 'spdlog.lib', 'zlib.lib') | ForEach-Object { Join-Path $lib $_ } | Where-Object { Test-Path -LiteralPath $_ }
  Push-Location $scratch
  try {
    $out = & cl.exe /nologo /EHsc /std:c++20 /MT /O2 /utf-8 /DWIN32 /DSPDLOG_FMT_EXTERNAL /DSPDLOG_COMPILED_LIB `
      ('/I' + (Join-Path $Source 'libespm\include')) ('/I' + (Join-Path $Source 'libespm\src')) `
      ('/I' + (Join-Path $Source 'viet\include')) ('/I' + (Join-Path $Source 'skymp5-server\cpp\server_guest_lib')) `
      ('/I' + $include) $test @sources @libs /Fe:test_light_plugins.exe 2>&1
    if ($LASTEXITCODE) { $out | Where-Object { $_ -match 'error' } | Select-Object -First 20 | Write-Output; throw 'test_light_plugins did not compile' }
    & .\test_light_plugins.exe $Data $Plugins $Ccc $ClientFull $ClientLight (Join-Path $scratch 'work')
    $code = $LASTEXITCODE
  } finally { Pop-Location }
} finally {
  Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
exit $code
