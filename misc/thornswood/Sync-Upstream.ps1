<#
.SYNOPSIS
  Merges upstream SkyMP's main into thornswood/engine-changes on the branch
  upstream-sync/next. It pushes that branch, starts its build, and opens or
  updates the pull request into thornswood/engine-changes.

.DESCRIPTION
  Thornswood #5154, run weekly by .github/workflows/thornswood-upstream-sync.yml.
  Until now an upstream fix reached Thornswood only when somebody remembered
  to merge it by hand. The last time was 5 Oct 2026, "Bring in
  skyrim-multiplayer/skymp main's three commits of 5 Oct", and it was done on
  main, which Thornswood does not build.

  Steps:
    1. fetch upstream's main. If thornswood/engine-changes already holds it,
       say so and stop green;
    2. merge it, with a merge commit, onto thornswood/engine-changes as
       upstream-sync/next. A conflict stops red, naming each file and the
       commands a person runs to resolve it;
    3. stop red, with the same commands, if the merge brings in a change
       under .github/workflows: GitHub refuses that push from a workflow's
       own token, and only a person may change what the workflows run;
    4. force-push upstream-sync/next, start thornswood-pr-windows.yml on it
       (a push made with this token starts no build by itself), and open or
       update the pull request.

  It runs nothing from upstream: git merges the files, and nothing in them is
  built or run here. They are built by thornswood-pr-windows.yml's read-only
  build-pr job, as any pull request is. The token is passed to git as a
  header for the one push, and never written into a remote URL or a file.

  The pull request merges with a merge commit, never a squash. A squash drops
  upstream's commits from the branch's history, so the next sync would merge
  them again and conflict.

  Exit 0: nothing new, or the pull request is open at the merge. 1: why not.

.EXAMPLE
  pwsh misc/thornswood/Sync-Upstream.ps1
  In the workflow, with GH_TOKEN set to the run's token.
#>
[CmdletBinding()]
param(
    [string]$Upstream = 'https://github.com/skyrim-multiplayer/skymp',
    [string]$UpstreamBranch = 'main',
    [string]$Repo = 'EvilPatrick06/skymp',
    [string]$Base = 'thornswood/engine-changes',
    [string]$Branch = 'upstream-sync/next',
    # Where the branch is pushed; a harness gives a folder.
    [string]$PushTo = "https://github.com/$Repo",
    [switch]$NoPush
)
$ErrorActionPreference = 'Stop'
function Stop-Here([string]$m) { Write-Host "::error title=Upstream sync::$($m -replace "`n", '%0A')"; Write-Host "STOP. $m"; exit 1 }
function Invoke-Git {
    $out = @(& git @args 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw ("git $($args -join ' ') failed: " + ($out -join ' ')) }
    return $out
}
$byHand = @"
To bring it in by hand, from a clone of ${Repo}:
  git fetch origin $Base
  git switch -c upstream-sync/by-hand origin/$Base
  git pull --no-rebase --no-ff $Upstream $UpstreamBranch
  (resolve each file git names, then git add them and git commit)
  git push -u origin upstream-sync/by-hand
and open its pull request into $Base, merged with a merge commit, never a squash.
"@

# 1. Upstream's main.
try {
    Invoke-Git fetch -q origin "+refs/heads/$($Base):refs/remotes/origin/$Base" | Out-Null
    Invoke-Git fetch -q $Upstream "+refs/heads/$($UpstreamBranch):refs/remotes/upstream/$UpstreamBranch" | Out-Null
} catch { Stop-Here $_.Exception.Message }
$up = "$(Invoke-Git rev-parse "refs/remotes/upstream/$UpstreamBranch")".Trim()
$baseSha = "$(Invoke-Git rev-parse "refs/remotes/origin/$Base")".Trim()
& git merge-base --is-ancestor $up $baseSha
if ($LASTEXITCODE -eq 0) {
    Write-Host ("{0} already holds upstream's {1} at {2}: nothing to bring in" -f $Base, $UpstreamBranch, $up.Substring(0, 12))
    exit 0
}
$new = @(Invoke-Git log --format='%h %s' "$baseSha..$up")
Write-Host ("upstream's {0} at {1} has {2} commit(s) {3} does not" -f $UpstreamBranch, $up.Substring(0, 12), $new.Count, $Base)

# 2. The merge.
Invoke-Git switch -q -C $Branch $baseSha | Out-Null
$msg = "Bring in skyrim-multiplayer/skymp $UpstreamBranch at $($up.Substring(0, 12)) (Thornswood #5154)"
& git -c user.name='github-actions[bot]' -c user.email='41898282+github-actions[bot]@users.noreply.github.com' merge -q --no-ff --no-edit -m $msg $up 2>&1 | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) {
    $clash = @(& git diff --name-only --diff-filter=U)
    & git merge --abort 2>$null
    Stop-Here ("upstream's $UpstreamBranch at $($up.Substring(0, 12)) does not merge into $Base by itself. These files clash:`n  " + ($clash -join "`n  ") + "`n$byHand")
}

# 3. A workflow change is a person's.
$flows = @(Invoke-Git diff --name-only $baseSha HEAD -- .github/workflows) + @(Invoke-Git log --format= --name-only "$baseSha..HEAD" -- .github/workflows) |
    Where-Object { $_ } | Sort-Object -Unique
if ($flows.Count) {
    Stop-Here ("upstream's $UpstreamBranch changes what the workflows run, which a workflow's own token may not push, and which a person reads first:`n  " + ($flows -join "`n  ") + "`n$byHand")
}
$merge = "$(Invoke-Git rev-parse HEAD)".Trim()
Write-Host ("merged as {0}" -f $merge.Substring(0, 12))
if ($NoPush) { Write-Host 'not pushed (-NoPush)'; exit 0 }

# 4. Push, build, pull request.
if (-not $env:GH_TOKEN) { Stop-Here 'GH_TOKEN is not set, so there is nothing to push with' }
$basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes('x-access-token:' + $env:GH_TOKEN))
& git -c "http.https://github.com/.extraheader=AUTHORIZATION: basic $basic" push -q --force $PushTo "$($merge):refs/heads/$Branch" 2>&1 | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) { Stop-Here "the push of $Branch was refused; its lines are above" }
& gh workflow run thornswood-pr-windows.yml -R $Repo --ref $Branch
if ($LASTEXITCODE -ne 0) { Stop-Here "$Branch is pushed at $merge, but its build could not be started" }

$title = "Bring in upstream SkyMP's $UpstreamBranch at $($up.Substring(0, 12))"
$body = New-Object System.Collections.Generic.List[string]
$body.Add("Made by misc/thornswood/Sync-Upstream.ps1, the weekly upstream sync (Thornswood #5154).")
$body.Add('')
$body.Add(("{0} commit(s) of upstream's {1} that {2} does not have:" -f $new.Count, $UpstreamBranch, $Base))
foreach ($l in @($new | Select-Object -First 40)) { $body.Add("- $l") }
if ($new.Count -gt 40) { $body.Add("- and $($new.Count - 40) more") }
$body.Add('')
$body.Add("Every change: https://github.com/$Repo/compare/$($baseSha.Substring(0, 12))...$($merge.Substring(0, 12))")
$body.Add('')
$body.Add('The build check is the thornswood-pr-windows.yml run this started on the branch: a workflow''s own push starts no build by itself.')
$body.Add('')
$body.Add('**Merge with a merge commit, never a squash.** A squash drops upstream''s commits from the branch, so the next sync would merge them again and conflict.')
$body.Add('')
$body.Add("Once merged, the push build of $Base builds it and Thornswood's engine-pin.yml moves the pin to it (Thornswood #5148).")
$file = Join-Path ([IO.Path]::GetTempPath()) ('upstream-sync-' + [Guid]::NewGuid().ToString('N') + '.json')
try {
    $open = @(& gh api "repos/$Repo/pulls?state=open&head=$($Repo.Split('/')[0]):$Branch" --jq '.[].number' | Where-Object { $_ -match '^\d+$' })
    if ($LASTEXITCODE -ne 0) { Stop-Here 'the open pull requests could not be read' }
    if ($open.Count) {
        [IO.File]::WriteAllText($file, ([ordered]@{ title = $title; body = ($body -join "`n") } | ConvertTo-Json -Compress))
        $r = @(& gh api -X PATCH "repos/$Repo/pulls/$($open[0])" --input $file --jq .html_url 2>&1)
        if ($LASTEXITCODE -ne 0) { Stop-Here ("pull request #$($open[0]) could not be updated: " + ($r -join ' ')) }
        Write-Host "pull request $($r -join '') is at $merge"
        exit 0
    }
    [IO.File]::WriteAllText($file, ([ordered]@{ title = $title; body = ($body -join "`n"); head = $Branch; base = $Base; maintainer_can_modify = $true } | ConvertTo-Json -Compress))
    $r = @(& gh api -X POST "repos/$Repo/pulls" --input $file --jq .html_url 2>&1)
    if ($LASTEXITCODE -ne 0) {
        if (($r -join ' ') -match 'not permitted to create or approve pull requests') {
            Stop-Here "GitHub refused to let this workflow open the pull request: the fork's Actions setting 'Allow GitHub Actions to create and approve pull requests' is off. $Branch is pushed at $merge and its build is running."
        }
        Stop-Here ("GitHub refused the pull request: " + ($r -join ' '))
    }
    Write-Host "opened pull request $($r -join '') at $merge"
    exit 0
} finally {
    Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
}
