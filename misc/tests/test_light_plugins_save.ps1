# Thornswood #1715. Builds the menu save tests with MSVC and runs them on the
# template SkyrimPlatform ships:
#   test_save_format      savefile reads and writes Legendary and Special
#                         Edition saves, light plugin list included, and lists
#                         the game's plugins without moving the save's
#   test_quest_run_data   savefile reads a quest run data item of type 0,
#                         which a new game saved after player.additem holds,
#                         and writes the save back byte for byte
#   test_vsval_unknown_size        savefile stops on a vsval of size type 3
#                         and names it (Thornswood #2001)
#   test_global_data_unknown_type  savefile stops writing global data of a
#                         type it does not know and names it (#2001)
#   test_menu_save_names  every form the template names still names its
#                         plugin once a client's plugins are listed, and the
#                         client's forms name the plugins the client meant
#   test_light_face_refs  Aemon Stark's face, KhisartinBeards beard included,
#                         is named through the save's light plugin list
#   test_menu_template    the template is a Special Edition save at form
#                         version 78 that LoadGame can build on, and LoadGame
#                         edits its player change form stored compressed or
#                         plain
#
#   -Source  the checkout whose savefile and LoadGame code is under test (this
#            one by default). The tests build against the code before the
#            change too, so the same run on an older checkout shows what was
#            broken.
#   -Build   a configured engine build folder: its vcpkg zlib and nlohmann
#            json are used, static with the static runtime.
# Prints each test's lines and a summary; exits 1 when one fails. Leaves
# nothing behind.
param(
  [string]$Source = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))),
  [string]$Build = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))) 'build'),
  [string]$Template = ''
)
$ErrorActionPreference = 'Stop'
$tests = Split-Path -Parent $PSCommandPath
if (-not $Template) { $Template = Join-Path $Source 'skyrim-platform\src\platform_se\skyrim_platform\assets\template.ess' }
$include = Join-Path $Build 'vcpkg_installed\x64-windows-sp\include'
$zlib = Join-Path $Build 'vcpkg_installed\x64-windows-sp\lib\zlib.lib'
if (-not (Test-Path -LiteralPath $zlib)) { throw "No vcpkg zlib in $Build; pass -Build with a configured engine build folder" }

function Get-Definition([string]$text, [int]$start) {
  $brace = $text.IndexOf('{', $start); $depth = 1; $end = $brace + 1
  while ($depth -gt 0 -and $end -lt $text.Length) {
    if ($text[$end] -eq '{') { $depth++ }
    if ($text[$end] -eq '}') { $depth-- }
    $end++
  }
  if ($depth) { throw 'Unterminated native function' }
  $text.Substring($start, $end - $start)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ tools' }
foreach ($line in (cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set")) {
  if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('thornswood-light-plugins-save-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$failed = @()
try {
  # The face builder and the save functions under test, unchanged from the source.
  $platform = Join-Path $Source 'skyrim-platform\src\platform_se\skyrim_platform'
  $api = [IO.File]::ReadAllText((Join-Path $platform 'LoadGameApi.cpp'))
  $parts = foreach ($signature in 'uint32_t RgbToAbgr(', 'SaveFile_::RefID FormIdToRefId(', 'std::unique_ptr<SaveFile_::ChangeFormNPC_> CreateChangeFormNpc(') {
    $start = $api.IndexOf($signature)
    if ($start -lt 0) { throw "Missing face API function: $signature" }
    Get-Definition $api $start
  }
  [IO.File]::WriteAllText((Join-Path $scratch 'LightFaceApiUnderTest.inc'), ($parts -join "`n"))
  $loadGame = [IO.File]::ReadAllText((Join-Path $platform 'LoadGame.cpp'))
  $parts = foreach ($name in 'FillChangeForm', 'WriteChangeForm') {
    $start = $loadGame.IndexOf('LoadGame::' + $name + '(')
    if ($start -lt 0) { throw "Missing native function: $name" }
    Get-Definition $loadGame ($loadGame.LastIndexOf("`n", $start - 1) + 1)
  }
  [IO.File]::WriteAllText((Join-Path $scratch 'LightFaceSaveFunctionsUnderTest.inc'), ($parts -join "`n"))
  # The player change form editing in LoadGame.cpp. A function the checkout
  # under test does not have is left out (before Thornswood #1715 it had no
  # ReadChangeFormData or RewriteChangeFormData).
  $parts = foreach ($name in 'FindSectionWithPlayerLocation', 'CreatePlayerLocation', 'Decompress', 'ReadChangeFormData',
                             'RewriteChangeFormData', 'EditChangeForm', 'Compress', 'WriteChangeForm', 'ModifyEssStructure') {
    $start = $loadGame.IndexOf('LoadGame::' + $name + '(')
    if ($start -lt 0) { continue }
    $lineStart = $loadGame.LastIndexOf("`n", $start - 1) + 1
    # A return type on a line of its own above the name
    if ($loadGame.Substring($lineStart, $start - $lineStart).Trim() -eq '') { $lineStart = $loadGame.LastIndexOf("`n", $lineStart - 2) + 1 }
    Get-Definition $loadGame $lineStart
  }
  [IO.File]::WriteAllText((Join-Path $scratch 'PlayerFormFunctionsUnderTest.inc'), ($parts -join "`n"))

  # savefile from the checkout under test, not a prebuilt library.
  $savefile = @('SFReader', 'SFWriter', 'SFStructure', 'SFSeekerOfDifferences', 'SFChangeFormNPC', 'SFChangeFormACHR') |
    ForEach-Object { Join-Path $Source ('savefile\src\' + $_ + '.cpp') }
  Push-Location $scratch
  try {
    foreach ($test in 'test_save_format', 'test_quest_run_data', 'test_vsval_unknown_size', 'test_global_data_unknown_type', 'test_menu_save_names', 'test_light_face_refs', 'test_menu_template') {
      $out = & cl.exe /nologo /EHsc /std:c++17 /MT /utf-8 ('/I' + $scratch) ('/I' + $include) ('/I' + (Join-Path $Source 'savefile\include')) `
        ('/I' + $tests) ('/I' + $platform) (Join-Path $tests ($test + '.cpp')) @savefile $zlib ('/Fe:' + $test + '.exe') 2>&1
      if ($LASTEXITCODE) {
        Write-Output "== $test does not build here:"
        $out | Where-Object { $_ -match 'error' } | Select-Object -First 5 | Write-Output
        $failed += $test
        continue
      }
      $work = Join-Path $scratch $test
      New-Item -ItemType Directory -Path $work | Out-Null
      Write-Output "== $test"
      & (Join-Path $scratch ($test + '.exe')) $Template $work | Where-Object { $_ -notmatch '^File size' }
      if ($LASTEXITCODE) { $failed += $test }
    }
  } finally { Pop-Location }
} finally {
  Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failed.Count) { Write-Output ('FAILED: ' + ($failed -join ', ')); exit 1 }
Write-Output "PASS light plugin save tests on $Template"
