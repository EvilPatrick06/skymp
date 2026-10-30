<#
.SYNOPSIS
  Fails when any GitHub Action this repository runs is named by a tag or a
  branch instead of a full length commit SHA (Thornswood #2761).

.DESCRIPTION
  A tag on an action is a moving pointer: whoever owns the action can repoint
  v4 at any commit, and nothing in this repository changes when they do. The
  engine Thornswood ships is built by thornswood-pr-windows.yml and the
  pr_base action it calls, so a repointed tag there is somebody else's code
  inside the server and the client every install gets, with no commit here to
  read. Thornswood's own workflows were pinned by commit for that reason
  (tests\test-action-pins.ps1 there); the fork's came from upstream with tags,
  and 33 of them were still tags on 19 Oct, the build path among them.

  WHAT IS READ. Every workflow in .github\workflows and every action.yml under
  .github\actions, upstream's included: the ones upstream's guard skips here
  are pinned too, so this has no list of files to leave out that could go
  stale. For each `uses:` it fails on
    - a ref that is not a 40 character SHA (a tag, a branch, a short SHA,
      none at all);
    - a SHA with no comment after it saying which tag or branch it was, which
      is what Dependabot (the github-actions entry in dependabot.yml on main)
      reads to move it;
    - the same action pinned to two different SHAs, which is how one file
      quietly stays behind when the others move.
  A local action (./.github/actions/...) is whatever the commit under test
  holds and has nothing to pin. A docker:// image must name its sha256 digest.

  Then the same rules are driven over made up files in a temp folder, so a
  reader that stopped matching cannot pass by finding nothing.

  It never reaches GitHub, so it says nothing about whether a SHA still exists.
  Moving a pin is looking the tag up and replacing both halves:
      git ls-remote https://github.com/actions/checkout refs/tags/v4 'refs/tags/v4^{}'

  Read only apart from its own temp folder. Exit 0 when every check passes,
  1 when any fails. Run from anywhere:
      pwsh -NoProfile -File misc/thornswood/Test-ActionPins.ps1
#>
[CmdletBinding()]
param([string]$Root)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }

$script:pass = 0
$script:fail = 0
function Yes([string]$what, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "PASS  $what" }
    else { $script:fail++; Write-Host "FAIL  $what" }
}
function Done {
    Write-Host ''
    Write-Host ("{0} passed, {1} failed" -f $script:pass, $script:fail)
    if ($script:fail) { exit 1 }
    exit 0
}

# Every `uses:` in one file's text, as it is written. A step's uses may be the
# first line of the step (- uses:) or a later one, and a job may call a
# reusable workflow with one, so every shape is read.
function Read-Uses([string]$text, [string]$file) {
    $out = New-Object System.Collections.ArrayList
    $lines = $text -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $m = [regex]::Match($lines[$i], '^\s*(?:-\s+)?uses:\s*(?<ref>[^\s#]+)\s*(?<note>#.*)?$')
        if (-not $m.Success) { continue }
        $ref = $m.Groups['ref'].Value.Trim("'", '"')
        if ($ref.StartsWith('./')) { continue }
        $at = $ref.LastIndexOf('@')
        [void]$out.Add([pscustomobject]@{
            File   = $file
            Line   = $i + 1
            Ref    = $ref
            Action = $(if ($at -gt 0) { $ref.Substring(0, $at) } else { $ref })
            Pin    = $(if ($at -gt 0) { $ref.Substring($at + 1) } else { '' })
            Note   = $m.Groups['note'].Value.Trim()
        })
    }
    return @($out.ToArray())
}

# What is wrong with a set of uses, one line each, or nothing when they are
# all pinned. This is the whole rule, driven over the real files and the made
# up ones below. Every call is wrapped in @(): under strict mode an empty
# result is $null, which has no Count.
function Get-PinFaults($uses) {
    $bad = New-Object System.Collections.ArrayList
    foreach ($u in @($uses)) {
        $where = "{0}:{1}  {2}" -f $u.File, $u.Line, $u.Ref
        if ($u.Ref.StartsWith('docker://')) {
            if ($u.Ref -notmatch '@sha256:[0-9a-f]{64}$') { [void]$bad.Add("$where is an image by tag, not by its sha256 digest") }
            continue
        }
        if (-not $u.Pin) { [void]$bad.Add("$where is not pinned to anything"); continue }
        if ($u.Pin -notmatch '^[0-9a-f]{40}$') { [void]$bad.Add("$where is a tag or a branch, not a full length commit SHA"); continue }
        if ($u.Note -notmatch '^#\s*\S') { [void]$bad.Add("$where has no comment saying which tag that SHA is") }
    }
    foreach ($g in @($uses | Where-Object { -not $_.Ref.StartsWith('docker://') } | Group-Object Action)) {
        $pins = @($g.Group | ForEach-Object { $_.Pin } | Sort-Object -Unique)
        if ($pins.Count -gt 1) {
            [void]$bad.Add(("{0} is pinned {1} different ways: {2}" -f $g.Name, $pins.Count,
                            (($g.Group | ForEach-Object { '{0}:{1} {2}' -f $_.File, $_.Line, $_.Pin }) -join ', ')))
        }
    }
    return @($bad.ToArray())
}

# ---- the real files ---------------------------------------------------------
$gh = Join-Path $Root '.github'
Yes ".github is at $gh" (Test-Path -LiteralPath $gh)
if (-not (Test-Path -LiteralPath $gh)) { Done }

$files = @(Get-ChildItem -LiteralPath (Join-Path $gh 'workflows') -File | Where-Object { $_.Extension -in '.yml', '.yaml' })
$actions = Join-Path $gh 'actions'
if (Test-Path -LiteralPath $actions) {
    $files += @(Get-ChildItem -LiteralPath $actions -Recurse -File | Where-Object { $_.Name -in 'action.yml', 'action.yaml' })
}
$files = @($files | Sort-Object FullName)
$rel = { param($f) $f.FullName.Substring($Root.Length).TrimStart('\', '/').Replace('\', '/') }

# The two files the shipped engine is built by, named, so a rename shows here
# instead of this reading what is left and reporting that it is clean.
foreach ($want in '.github/workflows/thornswood-pr-windows.yml', '.github/actions/pr_base/action.yml') {
    Yes "$want is read" (@($files | Where-Object { (& $rel $_) -eq $want }).Count -eq 1)
}

$all = @()
foreach ($f in $files) { $all += Read-Uses ([IO.File]::ReadAllText($f.FullName)) (& $rel $f) }
Yes ("the reader finds the actions in {0} files ({1} uses)" -f $files.Count, $all.Count) ($all.Count -ge 20)
Yes 'the engine build itself runs pinned actions (checkout and upload-artifact in thornswood-pr-windows.yml)' (
    @($all | Where-Object { $_.File -eq '.github/workflows/thornswood-pr-windows.yml' -and $_.Action -in 'actions/checkout', 'actions/upload-artifact' }).Count -ge 2)

$faults = @(Get-PinFaults $all)
Yes 'every action in every workflow and action.yml is pinned to a full length SHA with its tag in a comment' ($faults.Count -eq 0)
foreach ($b in $faults) { Write-Host "      $b" }

# Said out loud, so the log shows what is pinned to what.
foreach ($g in @($all | Group-Object Action | Sort-Object Name)) {
    Write-Host ("      {0}  {1}  {2}" -f $g.Name, $g.Group[0].Pin, $g.Group[0].Note)
}

# ---- the reader catches what it is for --------------------------------------
$sha = '11d5960a326750d5838078e36cf38b85af677262'
$other = '0717577d45739eb3c851188b29f50ed6c0b2194e'
$work = Join-Path ([IO.Path]::GetTempPath()) ('fork-action-pins-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    function Fake([string]$name, [string[]]$lines) {
        [IO.File]::WriteAllText((Join-Path $work $name), ($lines -join "`n"))
        return Read-Uses ([IO.File]::ReadAllText((Join-Path $work $name))) $name
    }

    $ok = @(Fake 'good.yml' @(
        'runs:', '  using: composite', '  steps:',
        "    - uses: actions/checkout@$sha # v4.4.0",
        '    - name: keep it',
        "      uses: actions/upload-artifact@$other # v7.0.1",
        "    - uses: 'docker://alpine@sha256:$('a' * 64)'"))
    Yes 'the reader finds every shape of uses, on the step line, under a name and quoted' ($ok.Count -eq 3)
    Yes 'and pinned ones are clean' (@(Get-PinFaults $ok).Count -eq 0)

    $f = @(Get-PinFaults @(Fake 'tag.yml' @('    steps:', '      - uses: actions/checkout@v4')))
    Yes 'a floating tag is caught' ($f.Count -eq 1 -and $f[0] -match 'not a full length commit SHA')

    Yes 'a branch name is caught' (@(Get-PinFaults @(Fake 'branch.yml' @('      uses: Pospelove/auto-merge-action@main'))).Count -eq 1)
    Yes 'a short SHA is caught' (@(Get-PinFaults @(Fake 'short.yml' @(('      - uses: actions/checkout@' + $sha.Substring(0, 8) + ' # v4')))).Count -eq 1)
    Yes 'a reusable workflow by tag is caught' (@(Get-PinFaults @(Fake 'reuse.yml' @('    uses: octo/repo/.github/workflows/build.yml@v1'))).Count -eq 1)

    $f = @(Get-PinFaults @(Fake 'bare.yml' @('      - uses: actions/checkout')))
    Yes 'an action with no ref at all is caught' ($f.Count -eq 1 -and $f[0] -match 'not pinned to anything')

    $f = @(Get-PinFaults @(Fake 'nonote.yml' @("      - uses: actions/checkout@$sha")))
    Yes 'a SHA with no tag in a comment is caught' ($f.Count -eq 1 -and $f[0] -match 'no comment saying which tag')

    $f = @(Get-PinFaults @(Fake 'image.yml' @('      - uses: docker://alpine:3.20')))
    Yes 'an image by tag is caught' ($f.Count -eq 1 -and $f[0] -match 'sha256 digest')

    Yes 'a local action is not asked for a pin' (@(Fake 'local.yml' @('      - uses: ./.github/actions/pr_base')).Count -eq 0)

    $drift = @()
    $drift += @(Fake 'a.yml' @("      - uses: actions/checkout@$sha # v4.4.0"))
    $drift += @(Fake 'b.yml' @("      - uses: actions/checkout@$other # v2.8.0"))
    $f = @(Get-PinFaults $drift)
    Yes 'the same action pinned two ways in two files is caught' ($f.Count -eq 1 -and $f[0] -match 'pinned 2 different ways')
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Done
