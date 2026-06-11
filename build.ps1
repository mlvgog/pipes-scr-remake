# Build script for the 3D Pipes screensaver recreation.
# Requires MSYS2 ucrt64 gcc (or any MinGW-w64 gcc on PATH as `gcc`).

$gcc = "C:\msys64\ucrt64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $cmd) { Write-Error "gcc not found"; exit 1 }
    $gcc = $cmd.Source
}

& $gcc -O2 -Wall -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -mwindows
if ($LASTEXITCODE -eq 0) {
    Write-Host "Built pipes.exe"
    Write-Host "To install as a screensaver: Copy-Item pipes.exe pipes.scr; then right-click pipes.scr -> Install"
}
exit $LASTEXITCODE
