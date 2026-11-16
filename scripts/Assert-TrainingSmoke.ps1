param([Parameter(Mandatory)][string]$ReportPath)
$ErrorActionPreference='Stop'
$report=Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
$checks=[ordered]@{}
$checks.ready=$report.ready -and $report.pluginLoaded
$checks.survived=-not $report.crashed
$checks.restored=$report.restored
$checks.queries=($report.queries.Count -eq 11) -and @($report.queries | Where-Object exitCode -ne 0).Count -eq 0
$checks.disableDeferred=$report.queries[2].output -match 'requested=0 effective=1'
$checks.disableApplied=$report.queries[4].output -match 'requested=0 effective=0'
$checks.enableDeferred=$report.queries[5].output -match 'requested=1 effective=0'
# Empty-server rounds can already be invalid before bot_kick. Both are protected;
# this does not claim to have tested an active human training round here.
$checks.botRemovalProtected=$report.queries[7].output -match 'invalid=bot-disconnect|state=3'
$checks.botRestored=$report.queries[8].output -match 'bots=1 bot-slot=\d+'
$checks.mapBootstrap=$report.queries[10].output -match 'effective=1.*bots=1 bot-slot=\d+'
foreach($entry in $checks.GetEnumerator()) { if(-not $entry.Value) { throw "Training smoke failed: $($entry.Key)" } }
[pscustomobject]@{label=$report.label;sha256=$report.pluginSha256;checks=$checks} | ConvertTo-Json -Depth 4
