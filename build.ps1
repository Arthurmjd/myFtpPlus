$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $root "bin"

New-Item -ItemType Directory -Force $bin | Out-Null

$commonFlags = @(
    "-std=gnu++20",
    "-municode",
    "-DUNICODE",
    "-D_UNICODE",
    "-Wall",
    "-Wextra",
    "-O2",
    "-Isrc"
)

Write-Host "Building server..."
& g++ @commonFlags src/common.cpp src/server.cpp -o (Join-Path $bin "server.exe") -lws2_32 -lbcrypt -lshell32

Write-Host "Building client..."
& g++ @commonFlags src/common.cpp src/client.cpp -o (Join-Path $bin "client.exe") -lws2_32 -lbcrypt -lcomctl32 -lcomdlg32 -lshell32 -lole32

Write-Host "Build finished:"
Write-Host "  $bin\\server.exe"
Write-Host "  $bin\\client.exe"

