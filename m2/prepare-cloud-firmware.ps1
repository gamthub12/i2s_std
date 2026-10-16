$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$config = Get-Content (Join-Path $PSScriptRoot 'android/app/google-services.json') -Raw | ConvertFrom-Json
$client = @($config.client | Where-Object { $_.client_info.android_client_info.package_name -eq 'vn.local.alarmspeaker' })
if ($client.Count -ne 1) { throw 'Expected one Android client for vn.local.alarmspeaker' }
$project = $config.project_info.project_id
$database = $config.project_info.firebase_url.TrimEnd('/')
if ($project -like 'demo-*' -or $database -notlike 'https://*') { throw 'A real Firebase project and HTTPS Database URL are required' }
$settings = [ordered]@{
    CONFIG_M2_FIREBASE_ENABLED = 'y'
    CONFIG_M2_FIREBASE_EMULATOR = 'n'
    CONFIG_M2_FIREBASE_SPARK = 'y'
    CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY = 'y'
    CONFIG_M2_FIREBASE_PROJECT_ID = '"' + $project + '"'
    CONFIG_M2_FIREBASE_NAMESPACE = '"' + ($database.Split('/')[2].Split('.')[0]) + '"'
    CONFIG_M2_FIREBASE_API_KEY = '"' + $client[0].api_key[0].current_key + '"'
    CONFIG_M2_FIREBASE_FUNCTIONS_BASE = '"https://asia-southeast1-' + $project + '.cloudfunctions.net"'
    CONFIG_M2_FIREBASE_DATABASE_BASE = '"' + $database + '"'
}
$content = [IO.File]::ReadAllText((Join-Path $root 'sdkconfig'))
foreach ($entry in $settings.GetEnumerator()) {
    $pattern = '(?m)^(?:' + [regex]::Escape($entry.Key) + '=.*|# ' + [regex]::Escape($entry.Key) + ' is not set)\r?$'
    $line = if ($entry.Value -eq 'n') { '# ' + $entry.Key + ' is not set' } else { $entry.Key + '=' + $entry.Value }
    if ([regex]::IsMatch($content, $pattern)) { $content = [regex]::Replace($content, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $line }) }
    else { $content += "`r`n" + $line }
}
[IO.File]::WriteAllText((Join-Path $root 'sdkconfig.cloud'), $content, [Text.UTF8Encoding]::new($false))
Write-Output "Prepared sdkconfig.cloud for $project; existing sdkconfig preserved."
