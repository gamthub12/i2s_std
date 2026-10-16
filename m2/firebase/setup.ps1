$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$m2Root = (Resolve-Path '..').Path
$tools = Join-Path $m2Root '.tools'
$jreDir = Join-Path $tools 'jre21'
$emulatorDir = Join-Path $tools 'firebase-emulators'
New-Item -ItemType Directory -Force -Path $jreDir, $emulatorDir | Out-Null

if (-not (Get-ChildItem -LiteralPath $jreDir -Directory -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    $zip = Join-Path $tools 'jre21.zip'
    Invoke-WebRequest -Uri 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse' -OutFile $zip
    Expand-Archive -LiteralPath $zip -DestinationPath $jreDir -Force
}

$jar = Join-Path $emulatorDir 'firebase-database-emulator-v4.11.2.jar'
$expectedHash = 'B70D99344CAF17C98B6F910FA8F6EDF32A7C016CB1035E8915F70D38901EB97F'
if (-not (Test-Path -LiteralPath $jar) -or (Get-FileHash -LiteralPath $jar -Algorithm SHA256).Hash -ne $expectedHash) {
    Invoke-WebRequest -Uri 'https://storage.googleapis.com/firebase-preview-drop/emulator/firebase-database-emulator-v4.11.2.jar' -OutFile $jar
}
if ((Get-FileHash -LiteralPath $jar -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Realtime Database Emulator hash mismatch'
}

if (-not (Test-Path -LiteralPath 'node_modules\firebase-tools')) { npm.cmd ci --ignore-scripts --no-audit --no-fund }
if (-not (Test-Path -LiteralPath 'functions\node_modules\firebase-admin')) {
    Push-Location 'functions'
    try { npm.cmd ci --ignore-scripts --no-audit --no-fund } finally { Pop-Location }
}
Write-Host 'Firebase Emulator dependencies are ready.'
