$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = '2.5.0'
$packageName = "VNCheater-$version-DesktopSplash-x64"
$buildRoot = Join-Path $repo 'build/Release'
$buildUi = Join-Path $buildRoot 'ui'
$distRoot = Join-Path $repo 'dist'
$exe = Join-Path $buildRoot 'VNCheater.exe'
$readme = Join-Path $repo 'README.md'
$licenseSource = Join-Path $repo 'vendor/webview2'

foreach ($required in @($exe, (Join-Path $buildUi 'index.html'), $readme,
    (Join-Path $licenseSource 'LICENSE.txt'), (Join-Path $licenseSource 'NOTICE.txt'))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required build/package input is missing: $required"
    }
}

function Get-TreeHashes([string] $root) {
    $resolved = (Resolve-Path -LiteralPath $root).Path.TrimEnd('\', '/')
    $result = @{}
    foreach ($file in (Get-ChildItem -LiteralPath $resolved -Recurse -File | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($resolved.Length).TrimStart('\', '/').Replace('\', '/')
        $result[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    return $result
}

function Assert-TreeMatches([string] $sourceRoot, [string] $builtRoot, [string] $label) {
    if (!(Test-Path -LiteralPath $builtRoot -PathType Container)) {
        throw "Built UI folder is missing: $builtRoot"
    }
    $sourceHashes = Get-TreeHashes $sourceRoot
    $builtHashes = Get-TreeHashes $builtRoot
    $paths = @($sourceHashes.Keys + $builtHashes.Keys | Sort-Object -Unique)
    $different = @(
        foreach ($path in $paths) {
            if (!$sourceHashes.ContainsKey($path) -or !$builtHashes.ContainsKey($path) -or
                $sourceHashes[$path] -ne $builtHashes[$path]) { $path }
        }
    )
    if ($different.Count -gt 0) {
        throw "Built UI is stale for $label. Re-run scripts/build.ps1 before packaging:`n - $($different -join "`n - ")"
    }
}

# Verify every source tree copied by build.ps1, plus the entry document.
Assert-TreeMatches (Join-Path $repo 'css') (Join-Path $buildUi 'css') 'css'
Assert-TreeMatches (Join-Path $repo 'js') (Join-Path $buildUi 'js') 'js'
Assert-TreeMatches (Join-Path $repo 'assets') (Join-Path $buildUi 'assets') 'assets'
$sourceIndexHash = (Get-FileHash -LiteralPath (Join-Path $repo 'index.html') -Algorithm SHA256).Hash.ToUpperInvariant()
$builtIndexHash = (Get-FileHash -LiteralPath (Join-Path $buildUi 'index.html') -Algorithm SHA256).Hash.ToUpperInvariant()
if ($sourceIndexHash -ne $builtIndexHash) { throw 'Built UI is stale for index.html. Re-run scripts/build.ps1 before packaging.' }

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$stagingParent = Join-Path $distRoot '.staging'
New-Item -ItemType Directory -Path $stagingParent -Force | Out-Null
$staging = Join-Path $stagingParent ("$packageName-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging -Force | Out-Null

# The staging path is unique and owned by this invocation; leave it available for inspection.
Copy-Item -LiteralPath $exe -Destination (Join-Path $staging 'VNCheater.exe')
Copy-Item -LiteralPath $buildUi -Destination (Join-Path $staging 'ui') -Recurse
Copy-Item -LiteralPath $readme -Destination (Join-Path $staging 'README.md')
$licenses = Join-Path $staging 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $licenseSource 'LICENSE.txt') -Destination (Join-Path $licenses 'WebView2-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $licenseSource 'NOTICE.txt') -Destination (Join-Path $licenses 'WebView2-NOTICE.txt')

$sumLines = @()
$sumLines += ((Get-FileHash -LiteralPath (Join-Path $staging 'VNCheater.exe') -Algorithm SHA256).Hash.ToUpperInvariant() + '  VNCheater.exe')
$uiRoot = Join-Path $staging 'ui'
foreach ($file in (Get-ChildItem -LiteralPath $uiRoot -Recurse -File | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($staging.Length).TrimStart('\', '/').Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    $sumLines += "$hash  $relative"
}
$sumLines | Set-Content -LiteralPath (Join-Path $staging 'SHA256SUMS.txt') -Encoding utf8

$archive = Join-Path $distRoot "$packageName.zip"
if (Test-Path -LiteralPath $archive -PathType Leaf) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archive -CompressionLevel Optimal

Write-Output "Staging: $staging"
Write-Output "Archive: $archive"
Get-FileHash -LiteralPath $archive -Algorithm SHA256
