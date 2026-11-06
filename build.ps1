param([switch]$Test)

$gcc = "C:\msys64\ucrt64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $cmd) { Write-Error "gcc not found"; exit 1 }
    $gcc = $cmd.Source
}
$env:PATH = (Split-Path $gcc) + ";" + $env:PATH

$warnings = @("-Wall", "-Wextra")
if ($Test) { $warnings += "-Werror" }

& $gcc -O2 @warnings -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -mwindows
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built pipes.exe"

if (-not $Test) {
    Write-Host "To install as a screensaver: Copy-Item pipes.exe pipes.scr; then right-click pipes.scr -> Install"
    exit 0
}

& $gcc -O2 @warnings -o test_pipes.exe tests/test_pipes.c -lopengl32 -lgdi32 -luser32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built test_pipes.exe"

& .\test_pipes.exe .\pipes.exe
exit $LASTEXITCODE
