$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$m2Root = (Resolve-Path '..').Path
$jreRoot = (Get-ChildItem -LiteralPath (Join-Path $m2Root '.tools\jre21') -Directory | Select-Object -First 1).FullName
$env:JAVA_HOME = $jreRoot
$env:PATH = (Join-Path $jreRoot 'bin') + ';' + $env:PATH
$env:FIREBASE_EMULATORS_PATH = Join-Path $m2Root '.tools\firebase-emulators'
$env:XDG_CONFIG_HOME = Join-Path $m2Root '.tools\config'
$env:XDG_CACHE_HOME = Join-Path $m2Root '.tools\cache'
$env:FUNCTIONS_DISCOVERY_TIMEOUT = '60000'
$env:npm_config_offline = 'true'
$env:CI = '1'
$env:NO_UPDATE_NOTIFIER = '1'
node .\node_modules\firebase-tools\lib\bin\firebase.js emulators:exec --project demo-alarm --only auth,database,functions 'node test/e2e.js'
exit $LASTEXITCODE
