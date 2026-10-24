<#
.SYNOPSIS
  Writes, into build\licences, the licence text of every vcpkg library whose
  code is in one of the client files, and licences.json, which says which
  library is in which file (Thornswood #3099).

.DESCRIPTION
  The fork's triplet links every vcpkg library statically
  (overlay_triplets\x64-windows-sp.cmake), so a library is either inside a
  client file or not in it at all, and a library under MIT, BSD and the like
  asks for its notice to go with every binary copy. vcpkg puts each port's
  text at vcpkg_installed\<triplet>\share\<port>\copyright, and nothing took it
  from there.

  WHAT IS MEASURED. Each native client file's own program database, the one
  the file names in its debug directory (the build links with /DEBUG:FULL,
  cmake\apply_default_settings.cmake), held to it by GUID and age. From it, by
  llvm-pdbutil:
    - every module the linker took and the library it took it from (dump
      -modules). A library's object is taken only when something in the file
      needs it, so antigo, which MpClientPlugin.dll is linked with and never
      calls, is not there;
    - every source file with code in the file (dump -l, the line tables,
      which keep only the code the linker kept) or inlined into it (dump -il),
      for the header-only libraries, such as nlohmann-json, whose code is
      compiled into the fork's own objects.
  A path is a port's when vcpkg's list of the port's files
  (vcpkg_installed\vcpkg\info\<port>_<version>_<triplet>.list) holds it, or
  when it is under the port's own buildtrees or packages folder, where vcpkg
  compiled it. A port whose headers only declare things (cmakerc, for one)
  leaves no code and is not listed.

  WHAT IT WRITES, into -Out (build\licences):
    <port>.txt       the port's share\<port>\copyright, byte for byte
    licences.json    the commit, the triplet, the llvm-pdbutil that read it,
                     each client file with the ports in it and the other
                     libraries it took modules from, and each port with its
                     version, the licence its vcpkg.json declares, the files
                     it is in, and its text's name, size and sha256
  A port in a client file with no copyright text stops it: that notice is
  owed and there is nothing to give.

  Thornswood's engine.yml records licences.json and the texts with the client
  build (its scripts\release\New-ClientLicenceRecord.ps1), and every download
  that carries one of these files carries the texts of the ports in it.
  .github\workflows\thornswood-pr-windows.yml runs this after every build and
  keeps the folder in the artifact Thornswood records.

.PARAMETER Build
  The build folder: dist\client and vcpkg_installed are under it.

.PARAMETER Out
  Where to write. Emptied first.
#>
[CmdletBinding()]
param(
  [string]$Build = (Join-Path $PSScriptRoot '..\..\build'),
  [string]$Out,
  [string]$Triplet = 'x64-windows-sp'
)
$ErrorActionPreference = 'Stop'
$Build = (Resolve-Path -LiteralPath $Build).ProviderPath
if (-not $Out) { $Out = Join-Path $Build 'licences' }

# The four native files of the six Thornswood records (its
# New-ClientBuildRecord.ps1). The other two, skymp5-client.js and
# TESModPlatform.pex, are not linked with anything from vcpkg.
$ClientFiles = @(
  'Data/SKSE/Plugins/MpClientPlugin.dll',
  'Data/SKSE/Plugins/SkyrimPlatform.dll',
  'Data/Platform/Distribution/RuntimeDependencies/SkyrimPlatformImpl.dll',
  'Data/Platform/Distribution/RuntimeDependencies/SkyrimPlatformCEF.exe.hidden'
)

$installed = Join-Path $Build ('vcpkg_installed\' + $Triplet)
$info = Join-Path $Build 'vcpkg_installed\vcpkg\info'
$status = Join-Path $Build 'vcpkg_installed\vcpkg\status'
foreach ($p in $installed, $info, $status) { if (-not (Test-Path -LiteralPath $p)) { throw "$p is not there: this is not a build folder vcpkg installed into" } }

$pdbutil = Get-Command llvm-pdbutil -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $pdbutil) {
  $p = Join-Path $env:ProgramFiles 'LLVM\bin\llvm-pdbutil.exe'
  if (Test-Path -LiteralPath $p) { $pdbutil = $p }
}
if (-not $pdbutil) { throw 'llvm-pdbutil is not on PATH or in Program Files\LLVM\bin, and nothing else here reads a program database' }
$pdbutilVersion = ((& $pdbutil --version 2>&1 | Out-String) -split "`n" | Where-Object { $_ -match 'version' } | Select-Object -First 1).Trim()

function Invoke-PdbUtil([string[]]$UtilArgs) {
  $o = & $pdbutil @UtilArgs 2>&1
  if ($LASTEXITCODE -ne 0) { throw ("llvm-pdbutil {0} exited {1}: {2}" -f ($UtilArgs -join ' '), $LASTEXITCODE, (($o | Select-Object -First 5) -join ' ')) }
  return @($o | ForEach-Object { "$_" })
}

# The installed ports, by vcpkg's status file: name, version, port version.
$ports = @{}
foreach ($para in ([IO.File]::ReadAllText($status) -split "(\r?\n){2,}")) {
  $f = @{}
  foreach ($line in ($para -split "\r?\n")) { if ($line -match '^([A-Za-z-]+):\s*(.*)$') { $f[$Matches[1]] = $Matches[2].Trim() } }
  if (-not $f['Package'] -or $f['Feature'] -or $f['Architecture'] -ne $Triplet) { continue }
  if ($f['Status'] -notmatch 'install ok installed') { continue }
  $ports[$f['Package']] = [ordered]@{ version = $f['Version']; portVersion = [int]("0" + $f['Port-Version']) }
}
if (-not $ports.Count) { throw "$status lists no port installed for $Triplet" }

# Every file vcpkg installed, to its port, by path under the triplet folder.
$owner = @{}
foreach ($l in @(Get-ChildItem -LiteralPath $info -Filter '*.list' -File)) {
  $port = $l.Name.Substring(0, $l.Name.IndexOf('_'))
  if (-not $ports.ContainsKey($port)) { continue }
  foreach ($line in [IO.File]::ReadAllLines($l.FullName)) {
    $t = $line.Trim().Replace('\', '/')
    if (-not $t.StartsWith($Triplet + '/', [StringComparison]::OrdinalIgnoreCase) -or $t.EndsWith('/')) { continue }
    $owner[$t.Substring($Triplet.Length + 1).ToLowerInvariant()] = $port
  }
}

# The port a path from a program database belongs to, or $null.
$marker = '/' + $Triplet.ToLowerInvariant() + '/'
function Get-PathPort([string]$Path) {
  $p = $Path.Replace('\', '/').ToLowerInvariant()
  $i = $p.LastIndexOf($marker)
  if ($i -ge 0) {
    $rel = $p.Substring($i + $marker.Length)
    if ($owner.ContainsKey($rel)) { return $owner[$rel] }
  }
  if ($p -match '/packages/([a-z0-9-]+)_' + [regex]::Escape($Triplet.ToLowerInvariant()) + '/') { if ($ports.ContainsKey($Matches[1])) { return $Matches[1] } }
  if ($p -match '/buildtrees/([a-z0-9-]+)/') { if ($ports.ContainsKey($Matches[1])) { return $Matches[1] } }
  return $null
}

# The program database a PE file names in its CodeView debug entry: path,
# GUID and age.
function Get-PeCodeView([string]$Path) {
  $b = [IO.File]::ReadAllBytes($Path)
  $pe = [BitConverter]::ToInt32($b, 0x3C)
  if ([BitConverter]::ToUInt32($b, $pe) -ne 0x00004550) { throw "$Path is not a PE file" }
  $coff = $pe + 4
  $sections = [BitConverter]::ToUInt16($b, $coff + 2)
  $optSize = [BitConverter]::ToUInt16($b, $coff + 16)
  $opt = $coff + 20
  if ([BitConverter]::ToUInt16($b, $opt) -ne 0x20B) { throw "$Path is not a 64-bit PE file" }
  $debugRva = [BitConverter]::ToUInt32($b, $opt + 112 + 6 * 8)
  $debugSize = [BitConverter]::ToUInt32($b, $opt + 112 + 6 * 8 + 4)
  if (-not $debugRva) { throw "$Path has no debug directory, so it names no program database" }
  $table = $opt + $optSize
  $at = -1
  for ($s = 0; $s -lt $sections; $s++) {
    $h = $table + 40 * $s
    $va = [BitConverter]::ToUInt32($b, $h + 12); $raw = [BitConverter]::ToUInt32($b, $h + 20); $rawSize = [BitConverter]::ToUInt32($b, $h + 16)
    if ($debugRva -ge $va -and $debugRva -lt $va + $rawSize) { $at = $raw + ($debugRva - $va); break }
  }
  if ($at -lt 0) { throw "$Path has a debug directory outside its sections" }
  for ($e = $at; $e -lt $at + $debugSize; $e += 28) {
    if ([BitConverter]::ToUInt32($b, $e + 12) -ne 2) { continue }
    $cv = [BitConverter]::ToUInt32($b, $e + 24)
    if ([Text.Encoding]::ASCII.GetString($b, $cv, 4) -ne 'RSDS') { throw "$Path has a CodeView entry that is not RSDS" }
    $guid = New-Object Guid (, [byte[]]$b[($cv + 4)..($cv + 19)])
    $age = [BitConverter]::ToUInt32($b, $cv + 20)
    $end = $cv + 24; while ($b[$end] -ne 0) { $end++ }
    return [pscustomobject]@{ Pdb = [Text.Encoding]::UTF8.GetString($b, $cv + 24, $end - $cv - 24); Guid = $guid; Age = $age }
  }
  throw "$Path has no CodeView debug entry, so it names no program database"
}

if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
$null = New-Item -ItemType Directory -Force -Path $Out

$fileRecord = [ordered]@{}
$inFiles = @{}
foreach ($rel in $ClientFiles) {
  $bin = Join-Path $Build ('dist\client\' + $rel.Replace('/', '\'))
  if (-not (Test-Path -LiteralPath $bin -PathType Leaf)) { throw "the build has no dist/client/$rel" }
  $cv = Get-PeCodeView $bin
  if (-not (Test-Path -LiteralPath $cv.Pdb -PathType Leaf)) { throw "$rel names $($cv.Pdb), and the build has no such file" }
  $pdb = (Resolve-Path -LiteralPath $cv.Pdb).ProviderPath
  $summary = Invoke-PdbUtil @('dump', '-summary', $pdb)
  $g = ($summary | Where-Object { $_ -match '^\s*GUID:' } | Select-Object -First 1) -replace '^\s*GUID:\s*', ''
  $a = ($summary | Where-Object { $_ -match '^\s*Age:' } | Select-Object -First 1) -replace '^\s*Age:\s*', ''
  if (([Guid]$g.Trim()) -ne $cv.Guid -or [uint32]$a.Trim() -ne $cv.Age) {
    throw "$pdb is GUID $g age $a, and $rel names $($cv.Guid) age $($cv.Age): it is not this file's program database"
  }

  $why = @{}   # the ports with code in the file
  $other = New-Object System.Collections.Generic.SortedSet[string] ([StringComparer]::OrdinalIgnoreCase)
  $libModules = @{}
  foreach ($line in (Invoke-PdbUtil @('dump', '-modules', $pdb))) {
    if ($line -notmatch '^\s*Obj: `(.*)`:\s*$') { continue }
    $obj = $Matches[1]
    if ($obj -notmatch '(?i)\.lib$') { continue }
    $port = Get-PathPort $obj
    if ($port) { $why[$port] = $true; $libModules[$port] = 1 + [int]$libModules[$port] }
    else { $null = $other.Add([IO.Path]::GetFileName($obj.Replace('\', '/'))) }
  }
  $sources = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::OrdinalIgnoreCase)
  # Line tables: a file's header, then its blocks at section:offset. A block
  # in section 0 would be code the linker dropped; none is counted.
  $current = $null
  foreach ($line in (Invoke-PdbUtil @('dump', '-l', $pdb))) {
    if ($line -match '^(\S.*?) \((?:MD5|SHA-?1|SHA-?256|None)[^)]*\)\s*$') { $current = $Matches[1]; continue }
    if ($current -and $line -match '^\s+([0-9A-Fa-f]{4}):[0-9A-Fa-f]{8}-' -and $Matches[1] -ne '0000') { $null = $sources.Add($current); $current = $null }
  }
  foreach ($line in (Invoke-PdbUtil @('dump', '-il', $pdb))) {
    if ($line -match '^\s*0x[0-9A-Fa-f]+\s*\|\s*\d+\s*\|\s*(.+?) \((?:MD5|SHA-?1|SHA-?256|None)[^)]*\)\s*$') { $null = $sources.Add($Matches[1]) }
  }
  $headerFiles = @{}
  foreach ($s in $sources) {
    $port = Get-PathPort $s
    if (-not $port) { continue }
    $why[$port] = $true
    $headerFiles[$port] = 1 + [int]$headerFiles[$port]
  }

  $names = @($why.Keys | Sort-Object)
  foreach ($n in $names) { if (-not $inFiles.ContainsKey($n)) { $inFiles[$n] = @() }; $inFiles[$n] += $rel }
  $fileRecord[$rel] = [ordered]@{
    pdb = [IO.Path]::GetFileName($cv.Pdb.Replace('\', '/'))
    ports = $names
    otherLibraries = @($other)
  }
  Write-Host ("{0}: {1} port(s), read from {2}" -f $rel, $names.Count, $cv.Pdb)
  foreach ($n in $names) {
    Write-Host ("  {0,-34} {1,5} module(s) from its library, {2,5} source file(s) with code" -f $n, [int]$libModules[$n], [int]$headerFiles[$n])
  }
  Write-Host ("  other libraries it took modules from: {0}" -f (@($other) -join ', '))
}

$portRecord = [ordered]@{}
$missing = @()
foreach ($n in @($inFiles.Keys | Sort-Object)) {
  $src = Join-Path $installed ('share\' + $n + '\copyright')
  if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { $missing += $n; continue }
  $to = Join-Path $Out ($n + '.txt')
  Copy-Item -LiteralPath $src -Destination $to
  $licence = $null
  $spdx = Join-Path $installed ('share\' + $n + '\vcpkg.spdx.json')
  if (Test-Path -LiteralPath $spdx) {
    $doc = [IO.File]::ReadAllText($spdx) | ConvertFrom-Json
    $pkg = @($doc.packages | Where-Object { $_.SPDXID -eq 'SPDXRef-port' }) | Select-Object -First 1
    # vcpkg writes LicenseRef-vcpkg-null for a port whose vcpkg.json says "license": null (makeid): not declared, like NOASSERTION.
    if ($pkg -and $pkg.licenseConcluded -and $pkg.licenseConcluded -notin @('NOASSERTION', 'LicenseRef-vcpkg-null')) { $licence = [string]$pkg.licenseConcluded }
  }
  $portRecord[$n] = [ordered]@{
    version = $ports[$n].version
    portVersion = $ports[$n].portVersion
    licence = $licence
    in = @($inFiles[$n])
    text = ($n + '.txt')
    bytes = (Get-Item -LiteralPath $to).Length
    sha256 = (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}
if ($missing.Count) { throw ("these ports are in a client file and vcpkg installed no copyright text for them: {0}. The notice is owed; give the port its text (vcpkg_install_copyright in its portfile)." -f ($missing -join ', ')) }

$commit = ''
$ErrorActionPreference = 'Continue'
$commit = "$(git -C (Join-Path $PSScriptRoot '..\..') rev-parse HEAD 2>$null)".Trim()
$ErrorActionPreference = 'Stop'
$record = [ordered]@{
  _note = 'The vcpkg libraries whose code is in each native client file, read from each file''s own program database, and each one''s licence text beside this file. Written by misc/thornswood/Write-ClientLicences.ps1 (Thornswood #3099).'
  commit = $commit
  triplet = $Triplet
  readWith = $pdbutilVersion
  files = $fileRecord
  ports = $portRecord
}
[IO.File]::WriteAllText((Join-Path $Out 'licences.json'), (($record | ConvertTo-Json -Depth 6) + "`n"), (New-Object Text.UTF8Encoding($false)))
Write-Host ("{0} port(s) in the client files; their texts and licences.json are in {1}" -f $portRecord.Count, $Out)
foreach ($n in $portRecord.Keys) {
  $r = $portRecord[$n]
  Write-Host ("  {0,-34} {1,-16} {2,-28} {3}" -f $n, $r.version, $(if ($r.licence) { $r.licence } else { '(not declared)' }), (@($r.in | ForEach-Object { $_.Substring($_.LastIndexOf('/') + 1) }) -join ', '))
}
