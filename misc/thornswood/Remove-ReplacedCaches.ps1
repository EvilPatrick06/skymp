<#
.SYNOPSIS
  Deletes the Actions caches of one kind that a newer one of the same kind
  replaced, so the repository keeps only the newest of each (Thornswood #5315).

.DESCRIPTION
  WHY. A cache saved under a key that names the commit or the run (CodeQL's
  overlay-base database, the compiler cache's store) is a new entry at every
  push, and nothing deletes the one before it, though a build only ever
  restores the newest. GitHub keeps 10 GB a repository and then evicts the
  entries used longest ago, whatever they are. Measured 1 Nov 2026: CodeQL
  saved 175 MB here on every push to thornswood/engine-changes (run
  41340857513), and Thornswood sat at 10.39 GB of 10 from its own. Deleting
  what was replaced leaves room for the caches the build reads.

  WHAT. Lists every cache on -Ref whose key starts with -Prefix. Each key must
  match -Shape, a regular expression with a group named "kind"; the newest
  cache of each kind (by its time made) is kept and the others are deleted.
  A key that starts with -Prefix and does not match -Shape is left alone and
  named, so a change in a key's form shows up instead of deleting the wrong
  thing. Prints what it kept, what it deleted and how much it freed.

  Needs GH_TOKEN with actions: write and GITHUB_REPOSITORY (both set in a
  workflow). Exits 1 when GitHub refuses the list or a delete.

.EXAMPLE
  ./misc/thornswood/Remove-ReplacedCaches.ps1 -Prefix ccache-windows-2022- -Shape '^(?<kind>ccache-windows-2022-)[0-9a-f]{40}$' -Ref refs/heads/thornswood/engine-changes
#>
param(
  [Parameter(Mandatory)][string]$Prefix,
  [Parameter(Mandatory)][string]$Shape,
  [Parameter(Mandatory)][string]$Ref
)
$ErrorActionPreference = 'Stop'
$repo = $env:GITHUB_REPOSITORY
if (-not $repo) { Write-Host '::error::GITHUB_REPOSITORY is not set'; exit 1 }
if ($Shape -notmatch '\(\?<kind>') { Write-Host "::error::-Shape has no group named kind: $Shape"; exit 1 }

$all = New-Object System.Collections.Generic.List[object]
for ($page = 1; ; $page++) {
  $json = gh api -X GET "repos/$repo/actions/caches" -f "key=$Prefix" -f "ref=$Ref" -f per_page=100 -f "page=$page"
  if ($LASTEXITCODE -ne 0) { Write-Host "::error::GitHub refused the list of $Prefix caches (gh exit $LASTEXITCODE)"; exit 1 }
  $got = @(($json | ConvertFrom-Json).actions_caches)
  foreach ($c in $got) { $all.Add($c) }
  if ($got.Count -lt 100) { break }
}

$kept = 0; $gone = 0; $freed = [long]0
foreach ($g in @($all | Where-Object { $_.key -match $Shape } | Group-Object { ([regex]::Match($_.key, $Shape)).Groups['kind'].Value })) {
  $byAge = @($g.Group | Sort-Object { [DateTime]$_.created_at } -Descending)
  Write-Host ('keeping {0} ({1:N0} MB)' -f $byAge[0].key, ($byAge[0].size_in_bytes / 1MB))
  $kept++
  foreach ($c in @($byAge | Select-Object -Skip 1)) {
    gh api -X DELETE "repos/$repo/actions/caches/$($c.id)" | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "::error::GitHub refused to delete $($c.key) (gh exit $LASTEXITCODE)"; exit 1 }
    Write-Host ('deleted {0} ({1:N0} MB)' -f $c.key, ($c.size_in_bytes / 1MB))
    $gone++; $freed += [long]$c.size_in_bytes
  }
}
foreach ($c in @($all | Where-Object { $_.key -notmatch $Shape })) {
  Write-Host "::warning::left alone, its key starts with $Prefix but is not the shape named: $($c.key)"
}
Write-Host ('{0}: {1} kept, {2} deleted, {3:N0} MB freed' -f $Prefix, $kept, $gone, ($freed / 1MB))
