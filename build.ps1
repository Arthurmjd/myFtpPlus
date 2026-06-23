$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $root "bin"

New-Item -ItemType Directory -Force $bin | Out-Null

$commonFlags = @(
    "-std=gnu++20",
    "-municode",
    "-DUNICODE",
    "-D_UNICODE",
    "-finput-charset=UTF-8",
    "-fexec-charset=UTF-8",
    "-Wall",
    "-Wextra",
    "-Wno-cast-function-type",
    "-O2",
    "-Isrc"
)

function Invoke-Compile {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string]$Output,
        [string[]]$Libraries
    )

    Write-Host "Building $Name..."
    & g++ @commonFlags @Sources -o $Output @Libraries
    if ($LASTEXITCODE -ne 0) {
        throw "$Name build failed."
    }
}

$serverSources = @(
    "src/shared/common.cpp",
    "src/platform/win32_util.cpp",
    "src/server/user_store.cpp",
    "src/server/core.cpp",
    "src/server/window.cpp",
    "src/server/main.cpp"
)
Invoke-Compile -Name "server" -Sources $serverSources -Output (Join-Path $bin "server.exe") -Libraries @(
    "-lws2_32",
    "-lbcrypt",
    "-lcomdlg32",
    "-lgdi32",
    "-lshell32",
    "-lole32"
)

$clientSources = @(
    "src/shared/common.cpp",
    "src/platform/win32_util.cpp",
    "src/client/protocol.cpp",
    "src/client/state.cpp",
    "src/client/window.cpp",
    "src/client/main.cpp"
)
Invoke-Compile -Name "client" -Sources $clientSources -Output (Join-Path $bin "client.exe") -Libraries @(
    "-lws2_32",
    "-lbcrypt",
    "-lcomctl32",
    "-lcomdlg32",
    "-lshell32",
    "-lole32"
)

Write-Host "Build finished:"
Write-Host "  $bin\\server.exe"
Write-Host "  $bin\\client.exe"
