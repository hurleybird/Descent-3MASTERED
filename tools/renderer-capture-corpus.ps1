param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Seeds,
    [Parameter(Mandatory=$true)][string]$Output,
    [string[]]$Saves = @('saveg000','saveg001','saveg002','saveg003','saveg004','saveg005','saveg006','saveg007'),
    [int]$Frame = 60,
    [uint32]$Seed = 12345,
    [int]$TimeoutSeconds = 120,
    [ValidateSet('clean','antialias','effects')][string]$Profile = 'effects',
    [ValidateSet(0,1,2,4)][int]$Supersampling = 0,
    [ValidateSet(-1,0,2,4,8)][int]$MsaaSamples = -1,
    [switch]$SceneInspection,
    [switch]$VertexLighting,
    [switch]$TerrainProbe,
    [switch]$SkyProbe,
    [switch]$SatelliteProbe,
    [switch]$AtmosphereProbe,
    [switch]$AxisProbe,
    [switch]$HistoryProbe,
    [switch]$DeformationProbe
)

# Run only copied installations and copied pilot/save data. Never point Runtime
# at Candidate or the normal installation: the game may write local cache files.
$ErrorActionPreference = 'Stop'
if ($AtmosphereProbe) { $SatelliteProbe = $true }
if ($AxisProbe -and -not $SceneInspection) { throw 'Axis probe requires the explicit scene inspection executor.' }
if ($DeformationProbe -and -not $SceneInspection) { throw 'Deformation probe requires the explicit scene inspection executor.' }
if ($TerrainProbe -and -not $SceneInspection) { throw 'Terrain probe requires the explicit scene inspection executor.' }
if ($SkyProbe -and -not $SceneInspection) { throw 'Sky probe requires the explicit scene inspection executor.' }
if ($SatelliteProbe -and -not $SceneInspection) { throw 'Satellite probe requires the explicit scene inspection executor.' }
if ($HistoryProbe -and -not $SceneInspection) { throw 'History probe requires the explicit scene inspection executor.' }
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$seedPath = (Resolve-Path -LiteralPath $Seeds).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if ($runtimePath -notmatch '^C:\\tmp\\D3RendererVerification\\[^\\]+$') {
    throw 'Runtime must be a dedicated installation under C:\tmp\D3RendererVerification.'
}
if (Test-Path -LiteralPath $outputPath) { throw 'Output already exists; captures are immutable.' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$exe = Join-Path $runtimePath 'Descent 3MASTERED.exe'
$registryPath = 'Registry::HKEY_CURRENT_USER\SOFTWARE\Outrage\Descent 3MASTERED'
function Read-Settings {
    $key = Get-Item -LiteralPath $registryPath
    $values = [ordered]@{}
    foreach ($name in ($key.GetValueNames() | Sort-Object)) { $values[$name] = $key.GetValue($name) }
    return ($values | ConvertTo-Json -Depth 8 -Compress)
}
$settings = Read-Settings
$sampleScale = if ($Supersampling -gt 0) { $Supersampling } elseif ($Profile -eq 'clean') { 1 } else { 2 }
$msaaCount = if ($MsaaSamples -ge 0) { $MsaaSamples } elseif ($Profile -eq 'clean') { 0 } else { 4 }
$settings | Set-Content -LiteralPath (Join-Path $outputPath 'settings.json') -Encoding UTF8
$manifest = [ordered]@{
    runtime = $runtimePath; profile = $Profile; frame = $Frame; seed = $Seed; scene_inspection = [bool]$SceneInspection
    supersampling = $sampleScale
    msaa_samples = $msaaCount
    deformation_probe = [bool]$DeformationProbe
    vertex_lighting = [bool]$VertexLighting
    terrain_probe = [bool]$TerrainProbe
    sky_probe = [bool]$SkyProbe
    satellite_probe = [bool]$SatelliteProbe
    atmosphere_probe = [bool]$AtmosphereProbe
    axis_probe = [bool]$AxisProbe
    history_probe = [bool]$HistoryProbe
    executable = (Get-FileHash -LiteralPath $exe).Hash
    shaders = (Get-FileHash -LiteralPath (Join-Path $runtimePath 'descent3mastered.hog')).Hash
    pilot = (Get-FileHash -LiteralPath (Join-Path $seedPath 'Jeff.plt')).Hash
    results = @()
}
$oldLog = $env:DESCENT3MASTERED_CAPTURE_LOG
try {
    foreach ($save in $Saves) {
        if ($save -notmatch '^saveg\d{3}$') { throw "Invalid save name: $save" }
        if ((Read-Settings) -cne $settings) { throw 'User graphics settings changed during the corpus run.' }
        Copy-Item -LiteralPath (Join-Path $seedPath 'Jeff.plt') -Destination (Join-Path $runtimePath 'Jeff.plt')
        # Loading increments a sibling .cnt file. Never load directly from seeds.
        $savePath = Join-Path $runtimePath "savegame\$save"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $savePath) | Out-Null
        Copy-Item -LiteralPath (Join-Path $seedPath "savegame\$save") -Destination $savePath
        Copy-Item -LiteralPath (Join-Path $seedPath "savegame\$save.cnt") -Destination "$savePath.cnt"
        $png = Join-Path $outputPath "$save.png"
        $log = Join-Path $outputPath "$save.log"
        $env:DESCENT3MASTERED_CAPTURE_LOG = $log
        $arguments = @('-background','-windowed','-nosound','-nomusic','-setdir',
            ('"{0}"' -f $runtimePath),'-pilot','Jeff','-loadgame',('"{0}"' -f $savePath),
            '-capture-frame',"$Frame",'-capture-dt','0.016666667','-capture-seed',"$Seed",'-capture-headlight',
            '-capture-output',('"{0}"' -f $png),'-render-resolution','1280','720',
            '-perpixellighting','-nomotionblur')
        if ($Profile -ne 'effects') { $arguments += @('-noao','-nobloom') }
        else { $arguments += @('-ao','-bloom') }
        $arguments += @('-ssaa',"$sampleScale",'-msaa',"$msaaCount")
        if ($SceneInspection) { $arguments += '-scene-inspection' }
        if ($HistoryProbe) { $arguments += '-scene-history-probe' }
        if ($VertexLighting) { $arguments += '-noperpixellighting' }
        if ($TerrainProbe) { $arguments += '-scene-terrain-probe' }
        if ($SkyProbe) { $arguments += '-scene-sky-probe' }
        if ($SatelliteProbe) { $arguments += '-scene-satellite-probe' }
        if ($AtmosphereProbe) { $arguments += '-scene-atmosphere-probe' }
        if ($AxisProbe) { $arguments += '-scene-axis-probe' }
        if ($DeformationProbe) { $arguments += '-scene-deformation-probe' }
        $started = Get-Date
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $runtimePath -WindowStyle Hidden -PassThru
        $timedOut = $false
        while (-not $process.HasExited) {
            if (((Get-Date) - $started).TotalSeconds -gt $TimeoutSeconds) {
                # This handle belongs exclusively to this harness, never another game.
                $timedOut = $true
                $process.Kill()
                break
            }
            Start-Sleep -Milliseconds 250
            $process.Refresh()
        }
        $process.WaitForExit()
        $logText = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
        # HandledWinMain returns 1 after a normal run, not conventional exit 0.
        $ok = -not $timedOut -and $process.ExitCode -eq 1 -and
            (Test-Path -LiteralPath $png) -and $logText.Contains('capture saved') -and
            $logText.Contains('capture headlight enabled') -and
            $logText.Contains("capture rng seed=$Seed") -and
            $logText.Contains('winmain application run returned')
        $manifest.results += [ordered]@{
            save=$save; save_hash=(Get-FileHash -LiteralPath $savePath).Hash
            success=$ok; exit_code=$process.ExitCode; timeout=$timedOut
            seconds=((Get-Date)-$started).TotalSeconds; arguments=$arguments
            image_hash=$(if (Test-Path -LiteralPath $png) { (Get-FileHash -LiteralPath $png).Hash } else { $null })
        }
        $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding UTF8
        Write-Output "$save success=$ok exit=$($process.ExitCode)"
        if (-not $ok) { throw "Capture failed for $save; see $log" }
    }
    if ((Read-Settings) -cne $settings) { throw 'Global settings changed during capture; comparison invalid.' }
} finally {
    $env:DESCENT3MASTERED_CAPTURE_LOG = $oldLog
}
