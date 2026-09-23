param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Seeds,
    [Parameter(Mandatory=$true)][string]$Output,
    [string[]]$Saves = @('saveg000','saveg001','saveg002','saveg003','saveg004','saveg005','saveg006','saveg007'),
    [int]$Frame = 60,
    [uint32]$Seed = 12345,
    [int]$TimeoutSeconds = 120,
    [ValidateSet('clean','antialias','effects','occlusion')][string]$Profile = 'effects',
    [ValidateSet(0,1,2,4)][int]$Supersampling = 0,
    [ValidateSet(-1,0,2,4,8)][int]$MsaaSamples = -1,
    [switch]$SceneInspection,
    [switch]$VertexLighting,
    [switch]$TerrainProbe,
    [switch]$SkyProbe,
    [switch]$SatelliteProbe,
    [switch]$AtmosphereProbe,
    [switch]$AxisProbe,
    [switch]$RingProbe,
    [switch]$DepthPolicyProbe,
    [switch]$ModelEffectsProbe,
    [switch]$StandaloneModelProbe,
    [switch]$ModelCollectionProbe,
    [switch]$DisplayViewProbe,
    [switch]$CockpitProbe,
    [switch]$ReticleProbe,
    [switch]$ReticleReference,
    [switch]$ObservationProbe,
    [switch]$MirrorCameraProbe,
    [switch]$MirrorSurfaceProbe,
    [switch]$ViewImageProbe,
    [switch]$ScorchProbe,
    [switch]$ScorchViewProbe,
    [switch]$MotionBlur,
    [switch]$LayerProbe,
    [switch]$ResourceProbe,
    [ValidateSet(-1,0,1,2)][int]$LodProbeTier = -1,
    [switch]$HistoryProbe,
    [switch]$DeformationProbe
)

# Run only copied installations and copied pilot/save data. Never point Runtime
# at Candidate or the normal installation: the game may write local cache files.
$ErrorActionPreference = 'Stop'
if ($AtmosphereProbe) { $SatelliteProbe = $true }
if ($DisplayViewProbe) { $ModelCollectionProbe = $true }
if ($ReticleReference -and -not $ReticleProbe) { throw 'Reticle reference requires the reticle inspection fixture.' }
if ($ObservationProbe -and -not $SceneInspection) { throw 'Observation inspection requires scene inspection; it never commits gameplay feedback.' }
if ($ReticleProbe -and (-not $SceneInspection -or $DisplayViewProbe -or $LayerProbe)) {
    throw 'Reticle probe requires scene inspection without other display fixtures.'
}
if ($CockpitProbe -and (-not $SceneInspection -or $DisplayViewProbe -or $LayerProbe -or $ModelCollectionProbe -or $LodProbeTier -ge 0)) {
    throw 'Cockpit probe requires scene inspection without other composition/LOD fixtures.'
}
if ($ModelCollectionProbe) { $StandaloneModelProbe = $true }
if ($StandaloneModelProbe) {
    if (-not $SceneInspection) { throw 'Standalone model probe requires scene inspection.' }
    $ModelEffectsProbe = $true
}
if ($LayerProbe -and -not $SceneInspection) { throw 'Layer probe requires scene inspection.' }
if ($RingProbe -and -not $SceneInspection) { throw 'Ring probe requires scene inspection.' }
if ($DepthPolicyProbe -and -not $SceneInspection) { throw 'Depth policy probe requires scene inspection.' }
if ($ResourceProbe -and -not $SceneInspection) { throw 'Resource probe requires scene inspection.' }
if ($LodProbeTier -ge 0 -and (-not $SceneInspection -or $HistoryProbe -or $ModelEffectsProbe -or $TerrainProbe -or $SkyProbe -or $SatelliteProbe -or $ScorchViewProbe)) {
    throw 'LOD probe requires isolated scene inspection and no other camera fixture.'
}
if ($ScorchViewProbe) {
    if (-not $SceneInspection -or $TerrainProbe -or $SkyProbe -or $SatelliteProbe -or $HistoryProbe -or $ModelEffectsProbe) {
        throw 'Scorch view probe requires scene inspection and no other camera probe.'
    }
    $ScorchProbe = $true
}
if ($AxisProbe -and -not $SceneInspection) { throw 'Axis probe requires the explicit scene inspection executor.' }
if ($ModelEffectsProbe -and (-not $SceneInspection -or $TerrainProbe -or $SkyProbe -or $SatelliteProbe -or $HistoryProbe)) {
    throw 'Model effects probe requires scene inspection and cannot combine with other camera probes.'
}
if ($DeformationProbe -and -not $SceneInspection) { throw 'Deformation probe requires the explicit scene inspection executor.' }
if ($TerrainProbe -and -not $SceneInspection) { throw 'Terrain probe requires the explicit scene inspection executor.' }
if ($ViewImageProbe -and -not $SceneInspection) { throw 'View image probe requires scene inspection.' }
if ($MirrorSurfaceProbe -and (-not $SceneInspection -or $MirrorCameraProbe)) { throw 'Mirror surface probe requires scene inspection and its own camera.' }
if ($MirrorCameraProbe -and (-not $SceneInspection -or $ObservationProbe -or $TerrainProbe -or $SkyProbe -or $SatelliteProbe -or $HistoryProbe -or $ScorchViewProbe -or $ModelEffectsProbe -or $LodProbeTier -ge 0)) {
    throw 'Mirror camera probe requires isolated scene inspection without other camera/observation probes.'
}
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
    inspection_banner_revision = $(if ($SceneInspection) { 2 } else { 0 })
    msaa_samples = $msaaCount
    deformation_probe = [bool]$DeformationProbe
    vertex_lighting = [bool]$VertexLighting
    terrain_probe = [bool]$TerrainProbe
    sky_probe = [bool]$SkyProbe
    satellite_probe = [bool]$SatelliteProbe
    atmosphere_probe = [bool]$AtmosphereProbe
    axis_probe = [bool]$AxisProbe
    ring_probe = [bool]$RingProbe
    depth_policy_probe = [bool]$DepthPolicyProbe
    ring_probe_revision = $(if ($RingProbe) { 2 } else { 0 })
    model_effects_probe = [bool]$ModelEffectsProbe
    standalone_model_probe = [bool]$StandaloneModelProbe
    model_collection_probe = [bool]$ModelCollectionProbe
    display_view_probe = [bool]$DisplayViewProbe
    cockpit_probe = [bool]$CockpitProbe
    reticle_probe = [bool]$ReticleProbe
    reticle_reference = [bool]$ReticleReference
    observation_probe = [bool]$ObservationProbe
    mirror_camera_probe = [bool]$MirrorCameraProbe
    mirror_camera_revision = $(if ($MirrorCameraProbe) { 4 } else { 0 })
    mirror_surface_probe = [bool]$MirrorSurfaceProbe
    planar_composition_revision = $(if ($SceneInspection) { 2 } else { 0 })
    view_image_probe = [bool]$ViewImageProbe
    view_image_revision = $(if ($ViewImageProbe) { 2 } else { 0 })
    cockpit_probe_revision = $(if ($CockpitProbe) { 4 } else { 0 })
    scorch_probe = [bool]$ScorchProbe
    scorch_view_probe = [bool]$ScorchViewProbe
    motion_blur = [bool]$MotionBlur
    layer_probe = [bool]$LayerProbe
    resource_probe = [bool]$ResourceProbe
    lod_probe_tier = $LodProbeTier
    lod_probe_revision = $(if ($LodProbeTier -ge 0) { 1 } else { 0 })
    model_effects_revision = $(if ($ModelEffectsProbe) { 2 } else { 0 })
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
            '-perpixellighting')
        if ($MotionBlur) { $arguments += '-motion-blur-new' }
        else { $arguments += '-nomotionblur' }
        if ($Profile -eq 'effects') { $arguments += @('-ao','-bloom') }
        elseif ($Profile -eq 'occlusion') { $arguments += @('-ao','-nobloom') }
        else { $arguments += @('-noao','-nobloom') }
        $arguments += @('-ssaa',"$sampleScale",'-msaa',"$msaaCount")
        if ($SceneInspection) { $arguments += '-scene-inspection' }
        if ($HistoryProbe) { $arguments += '-scene-history-probe' }
        if ($VertexLighting) { $arguments += '-noperpixellighting' }
        if ($TerrainProbe) { $arguments += '-scene-terrain-probe' }
        if ($SkyProbe) { $arguments += '-scene-sky-probe' }
        if ($SatelliteProbe) { $arguments += '-scene-satellite-probe' }
        if ($AtmosphereProbe) { $arguments += '-scene-atmosphere-probe' }
        if ($AxisProbe) { $arguments += '-scene-axis-probe' }
        if ($StandaloneModelProbe) { $arguments += '-scene-standalone-model-probe' }
        if ($ModelCollectionProbe) { $arguments += '-scene-model-collection-probe' }
        if ($DisplayViewProbe) { $arguments += '-scene-display-view-probe' }
        if ($CockpitProbe) { $arguments += '-scene-cockpit-probe' }
        if ($ReticleProbe) { $arguments += '-scene-reticle-probe' }
        if ($ReticleReference) { $arguments += '-scene-reticle-reference' }
        if ($ObservationProbe) { $arguments += '-scene-observation-probe' }
        if ($MirrorCameraProbe) { $arguments += '-scene-mirror-camera-probe' }
        if ($ViewImageProbe) { $arguments += '-scene-view-image-probe' }
        if ($MirrorSurfaceProbe) { $arguments += '-scene-mirror-surface-probe' }
        if ($RingProbe) { $arguments += '-scene-ring-probe' }
        if ($DepthPolicyProbe) { $arguments += '-scene-depth-policy-probe' }
        if ($ModelEffectsProbe) { $arguments += '-scene-model-effects-probe' }
        if ($ScorchProbe) { $arguments += '-capture-scorch' }
        if ($ScorchViewProbe) { $arguments += '-scene-scorch-view-probe' }
        if ($LayerProbe) { $arguments += '-scene-layer-probe' }
        if ($ResourceProbe) { $arguments += '-scene-resource-probe' }
        if ($LodProbeTier -ge 0) { $arguments += @('-scene-lod-probe',"$LodProbeTier") }
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
        $resourceValidated = $true
        if ($DepthPolicyProbe) { $resourceValidated = $logText.Contains('scene depth fixture columns=write,read,disabled,read_occluded') }
        if ($StandaloneModelProbe) { $resourceValidated = $logText.Contains('scene standalone model fixture instances=1') }
        if ($ModelCollectionProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene model collection fixture world=0 preview=1') }
        if ($DisplayViewProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene display view fixture target=display viewport=bottom_right') }
        if ($CockpitProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene cockpit prepared ') -and $logText.Contains('scene cockpit composed ') }
        if ($ReticleProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene reticle prepared ') }
        if ($ObservationProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene observation candidate ') -and $logText.Contains('scene observations sealed ') }
        if ($RingProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene ring fixture facing=1 planar_reverse=1 object=1 gravity=1') }
        if ($LodProbeTier -ge 0) {
            $resourceValidated = $resourceValidated -and $logText.Contains("scene LOD fixture tier=$LodProbeTier ") -and $logText.Contains('scene LOD visible ')
        }
        if ($ResourceProbe) {
            foreach ($transition in @(10,20,30,40,50)) {
                if ($transition -ge $Frame) { continue }
                $resourceValidated = $resourceValidated -and $logText.Contains("scene resource transition frame=$transition ")
                if ($Profile -in @('effects','occlusion') -or $MotionBlur) {
                    $resourceValidated = $resourceValidated -and $logText.Contains("scene motion frame=$transition camera_history=0 ")
                    $following = $transition + 1
                    if ($following -lt $Frame) {
                        $resourceValidated = $resourceValidated -and $logText.Contains("scene motion frame=$following camera_history=1 ")
                    }
                }
            }
        }
        # HandledWinMain returns 1 after a normal run, not conventional exit 0.
        $ok = -not $timedOut -and $resourceValidated -and (-not $MirrorCameraProbe -or $logText.Contains('scene mirror camera room=')) -and
            (-not $ViewImageProbe -or $logText.Contains('scene view image key=')) -and
            (-not $MirrorSurfaceProbe -or ($logText.Contains('scene mirror surface camera') -and $logText.Contains('scene view image key='))) -and $process.ExitCode -eq 1 -and
            (Test-Path -LiteralPath $png) -and $logText.Contains('capture saved') -and
            $logText.Contains('capture headlight enabled') -and
            $logText.Contains("capture rng seed=$Seed") -and
            $logText.Contains('winmain application run returned')
        $manifest.results += [ordered]@{
            save=$save; save_hash=(Get-FileHash -LiteralPath $savePath).Hash
            success=$ok; exit_code=$process.ExitCode; timeout=$timedOut
            resource_transitions_validated=$resourceValidated
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
