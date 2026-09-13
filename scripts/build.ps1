$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = '1.0.4191.47'
$expected = 'F492BBF547D0DA329553B6727435B677579B1E9F91CC9E4A1AD029366D5F23D0'
Push-Location $repo
try {
    if (!(Test-Path 'vendor/webview2/build/native/include/WebView2.h')) {
        New-Item -ItemType Directory -Force vendor | Out-Null
        Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile vendor/webview2.zip
        if ((Get-FileHash vendor/webview2.zip -Algorithm SHA256).Hash -ne $expected) { throw 'WebView2 SDK checksum mismatch.' }
        Expand-Archive vendor/webview2.zip vendor/webview2 -Force
    }
    cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    $output = Join-Path $repo 'build/Release/ui'
    New-Item -ItemType Directory -Force $output | Out-Null
    Copy-Item index.html $output -Force
    foreach ($folder in @('css','js','assets')) { Copy-Item $folder $output -Recurse -Force }
    Get-Item build/Release/VNCheater.exe | Select-Object FullName, Length
    Get-FileHash build/Release/VNCheater.exe -Algorithm SHA256
} finally { Pop-Location }
