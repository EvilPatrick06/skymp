<#
  Push-EnginePackage.ps1

  Keeps the build Thornswood records as a package of this repository on
  GitHub's container registry, ghcr.io/<owner>/thornswood-engine:<commit>,
  beside the artifact thornswood-engine-<commit> (Thornswood #459, #3399).

      .\misc\thornswood\Push-EnginePackage.ps1 -Folder <the gathered build> -Commit <sha>

  WHY A PACKAGE (Patrick, 30 Oct 2026: once Thornswood is public, use
  GitHub's Releases, Deployments and Packages). An artifact of this public
  repository is kept 90 days at most, so a pin older than that could not be
  shipped: no Ship machine could fetch the build its records name, and a
  rebuild is never the same bytes. A package version is kept until somebody
  deletes it, and a public package is read with no account at all.

  WHAT GOES UP. One OCI artifact: the gathered folder (fork-build.json and
  the build files at their paths) as one zip layer, an empty config, and
  the annotations that link it to this repository and name the commit. Its
  tag is the full commit. Nothing is pushed for a pull request: only the
  push build, the one Thornswood records, runs this.

  HOW. The registry's own HTTP API (the OCI distribution spec), with the
  run's token, so no tool is installed and no action is added: a bearer
  token for push on the one package, the zip and the config as blobs, each
  checked by its sha256, then the manifest under the commit. A tag already
  there for this commit is left as it is when it holds the same build (its
  fork-build.json, named in the manifest), and refused when it holds another:
  a commit's package is never replaced.

  Exit 0 pushed or already there. 1 with why.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$Repository = $env:GITHUB_REPOSITORY,
    [string]$Token = $env:GH_TOKEN,
    [string]$Registry = 'ghcr.io',
    # http only for a harness's stand-in registry.
    [string]$Scheme = 'https'
)
$ErrorActionPreference = 'Stop'

function Stop-Push([string]$Why) { Write-Host "::error title=Engine package::$Why"; Write-Host "STOPPED  $Why"; exit 1 }

if ($Commit -notmatch '^[0-9a-f]{40}$') { Stop-Push "'$Commit' is not a full commit" }
if (-not (Test-Path -LiteralPath (Join-Path $Folder 'fork-build.json') -PathType Leaf)) { Stop-Push "$Folder holds no fork-build.json" }
if (-not $Repository -or -not $Token) { Stop-Push 'GITHUB_REPOSITORY and GH_TOKEN are needed' }
$owner = $Repository.Split('/')[0].ToLowerInvariant()
$name = "$owner/thornswood-engine"
$base = "$Scheme`://$Registry/v2/$name"

# ---- a bearer token for push on this one package -----------------------------
$basic = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("$owner`:$Token"))
$tok = Invoke-RestMethod -Uri "$Scheme`://$Registry/token?scope=repository:$name`:pull,push&service=$Registry" -Headers @{ Authorization = "Basic $basic" }
$auth = @{ Authorization = 'Bearer ' + [string]$tok.token }

function Get-Sha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Send-Blob([string]$Path, [string]$Sha) {
    $digest = "sha256:$Sha"
    $have = Invoke-WebRequest -Method Head -Uri "$base/blobs/$digest" -Headers $auth -SkipHttpErrorCheck
    if ([int]$have.StatusCode -eq 200) { Write-Host "  had  $digest"; return }
    $start = Invoke-WebRequest -Method Post -Uri "$base/blobs/uploads/" -Headers $auth -SkipHttpErrorCheck
    if ([int]$start.StatusCode -ne 202) { Stop-Push "the registry refused an upload of $digest ($($start.StatusCode))" }
    $loc = [string]@($start.Headers['Location'])[0]
    if ($loc -notmatch '^https?://') { $loc = "$Scheme`://$Registry$loc" }
    $sep = if ($loc.Contains('?')) { '&' } else { '?' }
    $put = Invoke-WebRequest -Method Put -Uri ($loc + $sep + 'digest=' + [uri]::EscapeDataString($digest)) -Headers $auth -InFile $Path -ContentType 'application/octet-stream' -SkipHttpErrorCheck
    if ([int]$put.StatusCode -ne 201) { Stop-Push "the registry did not take $digest ($($put.StatusCode))" }
    Write-Host "  up   $digest"
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('engine-package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    $zip = Join-Path $work "thornswood-engine-$Commit.zip"
    Compress-Archive -Path (Join-Path $Folder '*') -DestinationPath $zip -CompressionLevel Optimal
    $zipSha = Get-Sha $zip
    # A zip is not the same bytes twice, so the build is known by its
    # fork-build.json, which names every file's sha256.
    $buildSha = Get-Sha (Join-Path $Folder 'fork-build.json')
    $cfg = Join-Path $work 'config.json'
    [IO.File]::WriteAllBytes($cfg, [Text.Encoding]::UTF8.GetBytes('{}'))
    $cfgSha = Get-Sha $cfg

    # ---- already there for this commit -----------------------------------------
    $accept = @{ Accept = 'application/vnd.oci.image.manifest.v1+json' } + $auth
    $old = Invoke-WebRequest -Uri "$base/manifests/$Commit" -Headers $accept -SkipHttpErrorCheck
    if ([int]$old.StatusCode -eq 200) {
        # A manifest's media type is not text to PowerShell, so it comes back as bytes.
        $m = $(if ($old.Content -is [byte[]]) { [Text.Encoding]::UTF8.GetString($old.Content) } else { [string]$old.Content }) | ConvertFrom-Json
        $was = [string]$m.annotations.'org.thornswood.fork-build.sha256'
        if ($was -eq $buildSha) { Write-Host "ALREADY  $Registry/$name`:$Commit holds this build"; exit 0 }
        Stop-Push "$Registry/$name`:$Commit already holds another build (its fork-build.json is $was); a commit's package is never replaced"
    }

    Send-Blob $cfg $cfgSha
    Send-Blob $zip $zipSha
    $manifest = [ordered]@{
        schemaVersion = 2
        mediaType = 'application/vnd.oci.image.manifest.v1+json'
        artifactType = 'application/vnd.thornswood.engine.v1'
        config = [ordered]@{ mediaType = 'application/vnd.oci.empty.v1+json'; digest = "sha256:$cfgSha"; size = (Get-Item -LiteralPath $cfg).Length }
        layers = @([ordered]@{
            mediaType = 'application/zip'; digest = "sha256:$zipSha"; size = (Get-Item -LiteralPath $zip).Length
            annotations = [ordered]@{ 'org.opencontainers.image.title' = "thornswood-engine-$Commit.zip" }
        })
        annotations = [ordered]@{
            'org.opencontainers.image.source' = "https://github.com/$Repository"
            'org.opencontainers.image.revision' = $Commit
            'org.thornswood.fork-build.sha256' = $buildSha
            'org.opencontainers.image.description' = "Thornswood's engine build of ${Commit}: the server build, the six client files, their licence texts and fork-build.json"
        }
    }
    $body = [Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Depth 6 -Compress))
    $put = Invoke-WebRequest -Method Put -Uri "$base/manifests/$Commit" -Headers $auth -Body $body -ContentType 'application/vnd.oci.image.manifest.v1+json' -SkipHttpErrorCheck
    if ([int]$put.StatusCode -ne 201) { Stop-Push "the registry did not take the manifest for $Commit ($($put.StatusCode))" }
    Write-Host ("PUSHED  {0}/{1}:{2}, the build as one zip of {3:N0} bytes (sha256 {4})" -f $Registry, $name, $Commit, (Get-Item -LiteralPath $zip).Length, $zipSha)
    exit 0
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
