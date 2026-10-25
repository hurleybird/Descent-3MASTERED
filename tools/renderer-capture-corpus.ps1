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
    [switch]$HudProbe,
    [switch]$HudMessageProbe,
    [switch]$HudSmallProbe,
    [switch]$AutomapProbe,
    [switch]$AutomapTerrainProbe,
    [switch]$AutomapTelcomProbe,
    [ValidateSet('none','guided','zoom','rear','letterbox','observer','hidden')][string]$HudViewProbe = 'none',
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
    [switch]$DeformationProbe,
    [string]$DemoFile = '',
    [ValidateSet('ordinary','retirement','save-export','save-settings','auxiliary-lifecycle','modal-ui','save-dialog','quicksave','decoded-movie','decoded-movie-paletted','decoded-movie-subtitles','screenshot-normal','screenshot-clean','screenshot-double','screenshot-tga','demo-movie')][string]$Scenario = 'ordinary'
)

# Run only copied installations and copied pilot/save data. Never point Runtime
# at Candidate or the normal installation: the game may write local cache files.
$ErrorActionPreference = 'Stop'
$scenarioFile = Join-Path $PSScriptRoot 'renderer-integration-scenarios.json'
$registry = Get-Content -LiteralPath $scenarioFile -Raw | ConvertFrom-Json
if ($registry.schemaVersion -ne 1) { throw 'Unsupported runtime scenario registry' }
$scenarioSpec = $registry.scenarios.PSObject.Properties[$Scenario].Value
if (!$scenarioSpec -or $Frame -lt $scenarioSpec.minimumFrame) { throw 'Scenario requires a later capture frame' }
if ($scenarioSpec.maximumFrame -and $Frame -gt $scenarioSpec.maximumFrame) { throw 'Scenario exceeds its bounded capture frame count' }
$inputIsDemo = $scenarioSpec.inputKind -eq 'demo'
if ($inputIsDemo) {
    if (!$DemoFile -or !(Test-Path -LiteralPath $DemoFile -PathType Leaf) -or $PSBoundParameters.ContainsKey('Saves')) {
        throw 'Demo scenario requires DemoFile and cannot also select saves'
    }
    $DemoFile = [IO.Path]::GetFullPath($DemoFile)
} elseif ($DemoFile) { throw 'DemoFile requires a demo scenario' }
if ($TimeoutSeconds -lt 1) { throw 'Capture timeout must be positive' }
foreach ($save in $Saves) { if ($save -notmatch '^saveg\d{3}$') { throw "Invalid save name: $save" } }
if ($scenarioSpec.quickSaveSlot -ne $null -and ($Saves.Count -ne 1 -or $Saves[0] -ne ('saveg{0:D3}' -f [int]$scenarioSpec.quickSaveSlot))) { throw 'Quicksave scenario must load its explicitly copied target slot' }
foreach ($pattern in $scenarioSpec.requiredLog) { [void][regex]::new($pattern) }
if ($scenarioSpec.subtitleAsset -and $scenarioSpec.subtitleAsset -notmatch '^[a-zA-Z0-9_-]+\.msb$') { throw 'Invalid subtitle fixture asset' }
if ($scenarioSpec.inspectionRequired -and !$SceneInspection) { throw 'Scenario requires explicit scene inspection' }
if ($Scenario -ne 'ordinary') {
    foreach ($name in $PSBoundParameters.Keys) {
        if (($name -like '*Probe*' -or $name -eq 'ReticleReference') -and $PSBoundParameters[$name]) {
            throw 'Named integration scenarios cannot mix additional geometry/camera/history fixtures'
        }
    }
}
if ($AutomapTerrainProbe) { $AutomapProbe = $true }
if ($AutomapTelcomProbe -and (-not $SceneInspection -or $AutomapProbe)) { throw 'TelCom fixture requires scene inspection and the normal world presentation.' }
if ($AutomapProbe -and (-not $SceneInspection -or $HudProbe -or $CockpitProbe -or $ReticleProbe -or $TerrainProbe -or $SkyProbe -or $HistoryProbe -or $MirrorSurfaceProbe -or $MirrorCameraProbe -or $ModelEffectsProbe -or $ObservationProbe -or $ViewImageProbe)) {
    throw 'Automap inspection owns a separate presentation and cannot combine with world/HUD fixtures.'
}
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
if ($HudProbe -and -not $SceneInspection) { throw 'HUD publication probe requires scene inspection.' }
if ($HudMessageProbe -and -not $HudProbe) { throw 'HUD message probe requires HUD publication.' }
if ($HudSmallProbe -and -not $HudProbe) { throw 'Small HUD probe requires HUD publication.' }
if ($HudViewProbe -ne 'none' -and (-not $HudProbe -or -not $ReticleProbe -or $ReticleReference -or $CockpitProbe -or $HudMessageProbe)) {
    throw 'HUD view policy probe requires HUD/reticle publication without cockpit, messages or legacy reference.'
}
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
# Seeds are read-only. Every destination we copy into must be a real local
# directory/file, never a junction into a protected installation or seed set.
foreach ($destination in @($runtimePath,$outputPath,(Join-Path $runtimePath 'savegame'),(Join-Path $runtimePath 'Jeff.plt'))) {
    $ancestor = $destination
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Mutable capture destination traverses a reparse point: $ancestor"
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
}
foreach ($readOnlyRoot in @($seedPath,$runtimePath)) {
    if ($outputPath.TrimEnd('\') -ieq $readOnlyRoot.TrimEnd('\') -or
        $outputPath.StartsWith($readOnlyRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Immutable capture evidence must be outside runtime and seed directories'
    }
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
    schema_version = 2
    scenario = $Scenario; scenario_revision = $scenarioSpec.revision
    scenario_registry_hash = (Get-FileHash -LiteralPath $scenarioFile).Hash
    harness_hash = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    export_comparison_hash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'renderer-export-images.ps1')).Hash
    input_kind = $(if ($inputIsDemo) {'demo'} else {'save'})
    preview_input_hash = $(if ($scenarioSpec.previewInput) { (Get-FileHash -LiteralPath (Join-Path $seedPath ('savegame/'+$scenarioSpec.previewInput))).Hash } else { $null })
    movie_input_hash = $(if ($scenarioSpec.movieInput) { (Get-FileHash -LiteralPath (Join-Path $runtimePath ('movies/'+$scenarioSpec.movieInput))).Hash } else { $null })
    subtitle_input_hash = $(if ($scenarioSpec.subtitleAsset) { (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot ('renderer-scene-tests/assets/'+$scenarioSpec.subtitleAsset))).Hash } else { $null })
    scenario_required_log = $scenarioSpec.requiredLog
    runtime = $runtimePath; profile = $Profile; frame = $Frame; seed = $(if ($inputIsDemo) {$null} else {$Seed}); scene_inspection = [bool]$SceneInspection
    rng_policy = $(if ($inputIsDemo) {'Native demo playback, including recorded event seeds; no capture-seed override'} else {'Fixed seed after native save load'})
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
    hud_probe = [bool]$HudProbe
    hud_message_probe = [bool]$HudMessageProbe
    hud_small_probe = [bool]$HudSmallProbe
    automap_probe = [bool]$AutomapProbe
    automap_terrain_probe = [bool]$AutomapTerrainProbe
    automap_telcom_probe = [bool]$AutomapTelcomProbe
    hud_view_probe = $HudViewProbe
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
$oldPerfLog = $env:DESCENT3MASTERED_FRAME_PERF_LOG
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding UTF8
if ($inputIsDemo) { $Saves = @('demo') }
try {
    foreach ($save in $Saves) {
        if (!$inputIsDemo -and $save -notmatch '^saveg\d{3}$') { throw "Invalid save name: $save" }
        if ((Read-Settings) -cne $settings) { throw 'User graphics settings changed during the corpus run.' }
        Copy-Item -LiteralPath (Join-Path $seedPath 'Jeff.plt') -Destination (Join-Path $runtimePath 'Jeff.plt')
        if ($scenarioSpec.previewInput) {
            if ($scenarioSpec.previewInput -notmatch '^saveg\d{3}$') { throw 'Invalid preview input name' }
            $previewPath = Join-Path $runtimePath ('savegame/'+$scenarioSpec.previewInput)
            if ((Test-Path -LiteralPath $previewPath) -and ((Get-Item -LiteralPath $previewPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Preview save is a reparse point' }
            Copy-Item -LiteralPath (Join-Path $seedPath ('savegame/'+$scenarioSpec.previewInput)) -Destination $previewPath
        }
        # Loading increments a sibling .cnt file. Never load directly from seeds.
        $savePath = if ($inputIsDemo) { Join-Path $outputPath 'input.dem' } else { Join-Path $runtimePath "savegame\$save" }
        foreach ($destination in @($savePath,"$savePath.cnt")) {
            if ((Test-Path -LiteralPath $destination) -and
                ((Get-Item -LiteralPath $destination -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Mutable save destination is a reparse point: $destination"
            }
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $savePath) | Out-Null
        if ($inputIsDemo) { Copy-Item -LiteralPath $DemoFile -Destination $savePath }
        else {
            Copy-Item -LiteralPath (Join-Path $seedPath "savegame\$save") -Destination $savePath
            Copy-Item -LiteralPath (Join-Path $seedPath "savegame\$save.cnt") -Destination "$savePath.cnt"
        }
        $png = Join-Path $outputPath "$save.png"
        $log = Join-Path $outputPath "$save.log"
        $env:DESCENT3MASTERED_CAPTURE_LOG = $log
        $env:DESCENT3MASTERED_FRAME_PERF_LOG = Join-Path $outputPath "$save.frames.tsv"
        $arguments = @('-background','-windowed','-nosound','-nomusic','-setdir',
            ('"{0}"' -f $runtimePath),'-pilot','Jeff','-loadgame',('"{0}"' -f $savePath),
            '-capture-frame',"$Frame",'-capture-dt','0.016666667','-capture-seed',"$Seed",'-capture-headlight',
            '-capture-output',('"{0}"' -f $png),'-render-resolution','1280','720',
            '-perpixellighting')
        if ($inputIsDemo) {
            $arguments[$arguments.IndexOf('-loadgame')] = '-playdemo'
            $seedIndex = $arguments.IndexOf('-capture-seed')
            $arguments = @($arguments[0..($seedIndex-1)]) + @($arguments[($seedIndex+2)..($arguments.Count-1)])
        }
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
        if ($HudProbe) { $arguments += '-scene-hud-probe' }
        if ($HudMessageProbe) { $arguments += '-scene-hud-message-probe' }
        if ($HudSmallProbe) { $arguments += '-scene-hud-small-probe' }
        if ($AutomapProbe) { $arguments += '-scene-automap-probe' }
        if ($AutomapTerrainProbe) { $arguments += '-scene-automap-terrain-probe' }
        if ($AutomapTelcomProbe) { $arguments += '-scene-automap-telcom-probe' }
        if ($HudViewProbe -ne 'none') { $arguments += @('-scene-hud-view-probe',$HudViewProbe) }
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
        $nativeSave = Join-Path $outputPath "$save.native"
        $uiImage = Join-Path $outputPath "$save.ui.png"
        $subtitleInput = Join-Path $outputPath "$save.captions.msb"
        if ($scenarioSpec.subtitleAsset) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('renderer-scene-tests/assets/'+$scenarioSpec.subtitleAsset)) -Destination $subtitleInput
        }
        foreach ($argument in $scenarioSpec.arguments) {
            $value = $argument.Replace('{save-output}',$nativeSave).Replace('{ui-output}',$uiImage).Replace('{movie-until}',[string]($Frame-1)).Replace('{subtitle-input}',$subtitleInput)
            if ($value -match '\s') { $value = '"'+$value+'"' }
            $arguments += $value
        }
        if ($arguments.Count -gt 63 -or @($arguments | Where-Object {[Text.Encoding]::UTF8.GetByteCount($_.Trim('"')) -gt 99}).Count) {
            throw 'Capture arguments exceed the shipped 64x100 module ABI'
        }
        $launchInputHash = (Get-FileHash -LiteralPath $savePath).Hash
        [ordered]@{scenario=$Scenario; revision=$scenarioSpec.revision
            executable=(Get-FileHash -LiteralPath $exe).Hash
            save_hash=$launchInputHash
            count_hash=$(if (!$inputIsDemo) { (Get-FileHash -LiteralPath "$savePath.cnt").Hash } else { $null })
            arguments=$arguments; started_utc=[DateTime]::UtcNow.ToString('o')
        } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath "$save.launch.json") -Encoding UTF8
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
        $scenarioValidated = $true
        foreach ($pattern in $scenarioSpec.requiredLog) {
            $scenarioValidated = $scenarioValidated -and ($logText -match $pattern)
        }
        $exports = @()
        if ($inputIsDemo) {
            . (Join-Path $PSScriptRoot 'renderer-export-images.ps1')
            $movieImages = [regex]::Matches($logText,'native screenshot saved path=(.+) width=\d+ height=\d+ scale=1 format=png')
            $scenarioValidated = $scenarioValidated -and $movieImages.Count -eq $Frame
            for ($i=0; $i -lt $movieImages.Count; ++$i) {
                $movie = $movieImages[$i].Groups[1].Value
                if ((Split-Path -Parent ([IO.Path]::GetFullPath($movie))) -ine $runtimePath) { throw 'Movie screenshot escaped private runtime' }
                $evidence = Join-Path $outputPath ("movie.{0:D6}.png" -f $i)
                Copy-Item -LiteralPath $movie -Destination $evidence
                $reference = $png+'.frame'+('{0:D6}' -f $i)+'.png'
                $comparison = Test-RendererExportImage -Reference $reference -Export $evidence
                $scenarioValidated = $scenarioValidated -and $comparison.mismatches -eq 0 -and $comparison.transparent -eq 0
                $exports += [ordered]@{frame=$i; movie_hash=(Get-FileHash -LiteralPath $evidence).Hash; preceding_display_hash=(Get-FileHash -LiteralPath $reference).Hash; comparison=$comparison}
            }
        }
        if ($scenarioSpec.exportScale) {
            . (Join-Path $PSScriptRoot 'renderer-export-images.ps1')
            $match = [regex]::Match($logText,'native screenshot saved path=(.+) width=\d+ height=\d+ scale=\d+ format=(png|tga)')
            if (!$match.Success) { $scenarioValidated = $false }
            else {
                $nativeImage = $match.Groups[1].Value
                if ((Split-Path -Parent ([IO.Path]::GetFullPath($nativeImage))) -ine $runtimePath) { throw 'Native screenshot escaped private runtime' }
                $evidence = Join-Path $outputPath ($save+'.native.'+$match.Groups[2].Value)
                Copy-Item -LiteralPath $nativeImage -Destination $evidence
                $comparison = Test-RendererExportImage -Reference ($png+'.frame000020.png') -Export $evidence -Scale $scenarioSpec.exportScale -Tga ([bool]$scenarioSpec.exportTga)
                $scenarioValidated = $scenarioValidated -and $comparison.mismatches -eq 0 -and $comparison.transparent -eq 0
                $exports += [ordered]@{image=(Split-Path -Leaf $evidence); hash=(Get-FileHash -LiteralPath $evidence).Hash; comparison=$comparison}
            }
        }
        if ($scenarioSpec.uiExport) {
            $uiExists = Test-Path -LiteralPath $uiImage
            $scenarioValidated = $scenarioValidated -and $uiExists
            $exports += [ordered]@{ui_image=$uiImage; hash=$(if ($uiExists) { (Get-FileHash -LiteralPath $uiImage).Hash } else { $null })}
            if ($scenarioSpec.movieInput) {
                foreach ($suffix in @('.previous.png','.source.png')) {
                    $exists = Test-Path -LiteralPath ($uiImage+$suffix)
                    $scenarioValidated = $scenarioValidated -and $exists
                    $exports += [ordered]@{movie_image=$uiImage+$suffix; hash=$(if ($exists) {(Get-FileHash -LiteralPath ($uiImage+$suffix)).Hash} else {$null})}
                }
                $frames = [regex]::Matches($logText,'scene decoded movie exported frame=(\d+) width=\d+ height=\d+ revision=(\d+)')
                $scenarioValidated = $scenarioValidated -and $frames.Count -eq 2
                if ($frames.Count -eq 2) {
                    $scenarioValidated = $scenarioValidated -and [int]$frames[0].Groups[1].Value -eq $scenarioSpec.movieCaptureFrame-1 -and
                        [int]$frames[1].Groups[1].Value -eq $scenarioSpec.movieCaptureFrame -and
                        [uint64]$frames[1].Groups[2].Value -gt [uint64]$frames[0].Groups[2].Value
                }
                if ($scenarioValidated) {
                    . (Join-Path $PSScriptRoot 'renderer-export-images.ps1')
                    $subtitleTop = -1
                    if ($scenarioSpec.subtitleOverlay) {
                        $caption = [regex]::Match($logText,('scene movie subtitle frame='+($scenarioSpec.movieCaptureFrame-1)+' lines=3 width=\d+ height=\d+ first_y=(\d+)'))
                        if (!$caption.Success) { throw 'Missing native subtitle layout receipt' }
                        $subtitleTop = [int]$caption.Groups[1].Value-2
                    }
                    $comparison = Test-RendererExportImage -Reference ($uiImage+'.source.png') -Export $uiImage -Upload1555 ([bool]$scenarioSpec.sourceUpload1555) -SubtitleTop $subtitleTop
                    $changed = (Get-FileHash -LiteralPath $uiImage).Hash -ne (Get-FileHash -LiteralPath ($uiImage+'.previous.png')).Hash
                    $scenarioValidated = $comparison.mismatches -eq 0 -and $comparison.transparent -eq 0 -and $changed
                    if ($scenarioSpec.subtitleOverlay) { $scenarioValidated = $scenarioValidated -and $comparison.intentional_subtitle_changes -gt 0 }
                    $exports += [ordered]@{decoded_source_comparison=$comparison; consecutive_frames_changed=$changed; intentional_subtitle_top=$subtitleTop}
                }
            }
        }
        if ($Scenario -eq 'quicksave') {
            $audit = [regex]::Matches($logText,'scene save thumbnail simulation state unchanged frame=\d+ evidence=(.+)')
            if ($audit.Count -ne 1) { $scenarioValidated = $false }
            else {
                $prefix = $audit[0].Groups[1].Value.Trim()
                if ($prefix -ine ($savePath+'.audit1')) { throw 'Quicksave evidence escaped copied input slot' }
                foreach ($kind in @('live-before','live-after','thumbnail','world')) {
                    Copy-Item -LiteralPath ($prefix+".$kind.png") -Destination ($nativeSave+".audit1.$kind.png")
                }
                Copy-Item -LiteralPath $savePath -Destination $nativeSave
            }
        }
        if ($Scenario -in @('save-export','save-settings','quicksave')) {
            $pairs = @(Get-ChildItem -LiteralPath $outputPath -Filter "$save.native.audit*.live-before.png")
            $minimumPairs = if ($Scenario -eq 'save-export') { 2 } else { 1 }
            $scenarioValidated = $scenarioValidated -and $pairs.Count -ge $minimumPairs -and (Test-Path -LiteralPath $nativeSave)
            foreach ($before in $pairs) {
                $after = $before.FullName.Replace('.live-before.png','.live-after.png')
                $thumbnail = $before.FullName.Replace('.live-before.png','.thumbnail.png')
                $world = $before.FullName.Replace('.live-before.png','.world.png')
                $missing = @(@($after,$thumbnail,$world) | Where-Object {!(Test-Path -LiteralPath $_)})
                if ($missing.Count) {
                    $scenarioValidated = $false
                    $exports += [ordered]@{before=$before.Name; missing=$missing}
                    continue
                }
                $beforeHash = (Get-FileHash -LiteralPath $before.FullName).Hash
                $afterHash = (Get-FileHash -LiteralPath $after).Hash
                $scenarioValidated = $scenarioValidated -and $beforeHash -eq $afterHash
                $exports += [ordered]@{before=$before.Name; before_hash=$beforeHash; after_hash=$afterHash
                    thumbnail_hash=(Get-FileHash -LiteralPath $thumbnail).Hash
                    world_hash=(Get-FileHash -LiteralPath $world).Hash}
            }
            if ($scenarioSpec.previousDisplayFrame -ne $null) {
                $reference = $png+'.frame'+('{0:D6}' -f [int]$scenarioSpec.previousDisplayFrame)+'.png'
                $beforeWorld = Join-Path $outputPath "$save.native.audit1.live-before.png"
                $published = (Get-FileHash -LiteralPath $reference).Hash
                $selected = (Get-FileHash -LiteralPath $beforeWorld).Hash
                $scenarioValidated = $scenarioValidated -and $published -eq $selected
                $exports += [ordered]@{presented_reference_hash=$published; before_world_selected_hash=$selected}
            }
        }
        if ($AutomapProbe) { $resourceValidated = $logText.Contains('scene automap prepared rooms=') }
        if ($AutomapTelcomProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene automap TelCom composed') }
        if ($DepthPolicyProbe) { $resourceValidated = $logText.Contains('scene depth fixture columns=write,read,disabled,read_occluded') }
        if ($StandaloneModelProbe) { $resourceValidated = $logText.Contains('scene standalone model fixture instances=1') }
        if ($ModelCollectionProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene model collection fixture world=0 preview=1') }
        if ($DisplayViewProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene display view fixture target=display viewport=bottom_right') }
        if ($CockpitProbe) { $resourceValidated = $resourceValidated -and $logText.Contains('scene cockpit prepared ') -and $logText.Contains('scene cockpit composed ') }
        if ($ReticleProbe -and $HudViewProbe -notin @('rear','letterbox','observer','hidden')) { $resourceValidated = $resourceValidated -and $logText.Contains('scene reticle prepared ') }
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
        $ok = -not $timedOut -and $resourceValidated -and $scenarioValidated -and (-not $MirrorCameraProbe -or $logText.Contains('scene mirror camera room=')) -and
            (-not $HudProbe -or $logText.Contains('scene HUD items prepared')) -and
            (-not $HudMessageProbe -or $logText.Contains('scene HUD messages prepared')) -and
            ($HudViewProbe -eq 'none' -or $logText.IndexOf("scene HUD view policy mode=$HudViewProbe",[StringComparison]::OrdinalIgnoreCase) -ge 0) -and
            (-not $ViewImageProbe -or $logText.Contains('scene view image key=')) -and
            (-not $MirrorSurfaceProbe -or ($logText.Contains('scene mirror surface camera') -and $logText.Contains('scene view image key='))) -and $process.ExitCode -eq 1 -and
            (Test-Path -LiteralPath $png) -and $logText.Contains('capture saved') -and
            $logText.Contains('capture headlight enabled') -and
            ($(if ($inputIsDemo) { $logText.Contains('playdemo accepted path=') } else { $logText.Contains("capture rng seed=$Seed") })) -and
            $logText.Contains('winmain application run returned')
        $manifest.results += [ordered]@{
            save=$save; save_hash=$launchInputHash
            final_input_hash=(Get-FileHash -LiteralPath $savePath).Hash
            success=$ok; exit_code=$process.ExitCode; timeout=$timedOut
            resource_transitions_validated=$resourceValidated
            scenario_validated=$scenarioValidated; exports=$exports
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
    $env:DESCENT3MASTERED_FRAME_PERF_LOG = $oldPerfLog
}
