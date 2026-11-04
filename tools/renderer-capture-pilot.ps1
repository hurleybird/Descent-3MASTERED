param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Logo='',
    [ValidateRange(0,60)][double]$Time=0,
    [ValidateSet(1,2,4)][int]$Supersampling=2,
    [ValidateSet(0,2,4,8)][int]$MsaaSamples=4
)
$ErrorActionPreference='Stop'
$runtimePath=[IO.Path]::GetFullPath($Runtime)
$outputPath=[IO.Path]::GetFullPath($Output)
$exe=Join-Path $runtimePath 'Descent 3MASTERED.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Missing private runtime executable' }
foreach ($path in @($runtimePath,$outputPath)) {
    $cursor=$path
    while ($cursor) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Capture paths cannot traverse links'
        }
        $cursor=Split-Path -Parent $cursor
    }
}
if (Test-Path -LiteralPath $outputPath) { throw 'Capture output must be new' }
if ($Logo) {
    if ($Logo -notmatch '^[a-zA-Z0-9_-]+\.(ogf|oaf)$') { throw 'Logo must be a private graphics filename' }
    $logoPath=Join-Path $runtimePath ('custom/graphics/'+$Logo)
    if (!(Test-Path -LiteralPath $logoPath -PathType Leaf)) { throw 'Missing private logo fixture' }
    for ($cursor=$logoPath; $cursor; $cursor=Split-Path -Parent $cursor) {
        if ((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Logo cannot traverse a link' }
    }
}
function PilotHashes {
    @((Get-ChildItem -LiteralPath $runtimePath -Filter '*.plt' -File | Sort-Object Name) | ForEach-Object {
        $_.Name+':'+(Get-FileHash -LiteralPath $_.FullName).Hash
    }) -join '|'
}
$before=PilotHashes
if (!$before) { throw 'Private runtime must contain a pilot to verify persistence isolation' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$png=Join-Path $outputPath 'pilot.png'
$log=Join-Path $outputPath 'pilot.log'
$arguments=@('-background','-windowed','-nosound','-nomusic','-nointro','-setdir',('"'+$runtimePath+'"'),
    '-pilot','Jeff','-capture-frame','1','-capture-pilot-ship','-capture-pilot-output',('"'+$png+'"'),
    '-render-resolution','1280','720','-ssaa',"$Supersampling",'-msaa',"$MsaaSamples",'-scene-inspection','-scene-gl-audit',
    '-capture-pilot-time',$Time.ToString('R',[Globalization.CultureInfo]::InvariantCulture))
if ($Logo) { $arguments+=@('-capture-pilot-logo',$Logo) }
foreach ($argument in $arguments) { if ([Text.Encoding]::UTF8.GetByteCount($argument) -ge 100) { throw 'Argument exceeds native ABI bound' } }
$manifest=[ordered]@{executable=(Get-FileHash -LiteralPath $exe).Hash;arguments=$arguments;pilot_before=$before;
    logo_hash=$(if ($Logo) {(Get-FileHash -LiteralPath $logoPath).Hash} else {$null})}
$oldLog=$env:DESCENT3MASTERED_CAPTURE_LOG
$process=$null
try {
    $env:DESCENT3MASTERED_CAPTURE_LOG=$log
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $runtimePath -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(45000)) { throw 'Private pilot capture timed out' }
    $manifest.exit=$process.ExitCode; $manifest.pilot_after=PilotHashes
    if ($manifest.pilot_after -ne $before) { throw 'Pilot capture changed persisted pilot files' }
    $content=Get-Content -LiteralPath $log -Raw
    if ($content -match 'Error:|scene GL error' -or !(Test-Path -LiteralPath $png) -or
        $content -notmatch 'scene UI ship preview .+owned=1' -or
        $content -notmatch 'capture run skipped persistent settings save' -or
        ($Logo -and $content -notmatch 'scene UI ship logo time=')) { throw 'Pilot capture evidence failed' }
    $manifest.image_hash=(Get-FileHash -LiteralPath $png).Hash
    $manifest.success=$true
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); [void]$process.WaitForExit(3000) }
    $env:DESCENT3MASTERED_CAPTURE_LOG=$oldLog
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath 'launch.json')
}
[pscustomobject]@{image=$png;success=$manifest.success}
