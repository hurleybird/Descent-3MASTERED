param(
    [Parameter(Mandatory=$true)][string]$Reference,
    [Parameter(Mandatory=$true)][string]$Actual,
    [Parameter(Mandatory=$true)][string]$Report,
    [string]$Save,
    [string]$Region,
    [switch]$DiagnosticComparison
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    $forwarded = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Reference',$Reference,'-Actual',$Actual,'-Report',$Report)
    if ($DiagnosticComparison) { $forwarded += '-DiagnosticComparison' }
    if ($Save) { $forwarded += @('-Save',$Save) }
    if ($Region) { $forwarded += @('-Region',$Region) }
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" @forwarded
    if ($LASTEXITCODE -ne 0) { throw 'Image comparison failed in Windows PowerShell.' }
    return
}
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class RendererImageComparison {
    public static double[] Measure(string leftPath, string rightPath, int[] region) {
        using (var left = new Bitmap(leftPath))
        using (var right = new Bitmap(rightPath)) {
            if (left.Size != right.Size) throw new Exception("Image dimensions differ");
            var rect = new Rectangle(0, 0, left.Width, left.Height);
            var sample = region.Length == 0 ? rect : new Rectangle(region[0],region[1],region[2],region[3]);
            if (sample.Width <= 0 || sample.Height <= 0 || sample.X < 0 || sample.Y < 0 ||
                (long)sample.X+sample.Width > left.Width || (long)sample.Y+sample.Height > left.Height)
                throw new Exception("Comparison region is outside the image");
            var a = left.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var b = right.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                var rowA = new byte[left.Width * 4];
                var rowB = new byte[left.Width * 4];
                long sum = 0, different = 0, aboveTwo = 0;
                int maximum = 0;
                int minX = left.Width, minY = left.Height, maxX = -1, maxY = -1;
                for (int y = sample.Top; y < sample.Bottom; ++y) {
                    Marshal.Copy(IntPtr.Add(a.Scan0, y*a.Stride), rowA, 0, rowA.Length);
                    Marshal.Copy(IntPtr.Add(b.Scan0, y*b.Stride), rowB, 0, rowB.Length);
                    for (int x = sample.Left*4; x < sample.Right*4; x += 4) {
                        int peak = 0;
                        for (int c = 0; c < 3; ++c) {
                            int delta = Math.Abs(rowA[x+c] - rowB[x+c]);
                            sum += delta; peak = Math.Max(peak, delta);
                        }
                        if (peak != 0) ++different;
                        if (peak > 2) {
                            ++aboveTwo;
                            minX = Math.Min(minX,x/4); maxX = Math.Max(maxX,x/4);
                            minY = Math.Min(minY,y); maxY = Math.Max(maxY,y);
                        }
                        maximum = Math.Max(maximum, peak);
                    }
                }
                double pixels = (double)sample.Width * sample.Height;
                return new double[] {left.Width, left.Height, sum/(pixels*3),
                    different/pixels*100, aboveTwo/pixels*100, maximum,
                    maxX < 0 ? -1 : minX,maxX < 0 ? -1 : minY,maxX,maxY};
            } finally { left.UnlockBits(a); right.UnlockBits(b); }
        }
    }
}
'@
$referencePath = (Resolve-Path -LiteralPath $Reference).Path
[int[]]$sampleRegion = @()
if ($Region) {
    if ($Region -notmatch '^\d+,\d+,\d+,\d+$') { throw 'Region must be x,y,width,height.' }
    $sampleRegion = $Region.Split(',') | ForEach-Object { [int]$_ }
}
$actualPath = (Resolve-Path -LiteralPath $Actual).Path
$referenceManifest = Get-Content -LiteralPath (Join-Path $referencePath 'manifest.json') -Raw | ConvertFrom-Json
$actualManifest = Get-Content -LiteralPath (Join-Path $actualPath 'manifest.json') -Raw | ConvertFrom-Json
if ($Save) {
    $selectedReference = @($referenceManifest.results | Where-Object save -eq $Save)
    $selectedActual = @($actualManifest.results | Where-Object save -eq $Save)
    if ($selectedReference.Count -ne 1 -or $selectedActual.Count -ne 1) { throw 'Requested save must occur exactly once in each corpus.' }
    # Filter only the in-memory comparison, never rewrite capture evidence.
    $referenceManifest.results = $selectedReference
    $actualManifest.results = $selectedActual
}
$referenceScale = if ($referenceManifest.supersampling) { $referenceManifest.supersampling } elseif ($referenceManifest.profile -eq 'clean') { 1 } else { 2 }
$actualScale = if ($actualManifest.supersampling) { $actualManifest.supersampling } elseif ($actualManifest.profile -eq 'clean') { 1 } else { 2 }
if ($referenceScale -ne $actualScale -and -not $DiagnosticComparison) { throw 'Supersampling differs; this is not a parity comparison.' }
$referenceMsaa = if ($null -ne $referenceManifest.msaa_samples) { $referenceManifest.msaa_samples } elseif ($referenceManifest.profile -eq 'clean') { 0 } else { 4 }
$actualMsaa = if ($null -ne $actualManifest.msaa_samples) { $actualManifest.msaa_samples } elseif ($actualManifest.profile -eq 'clean') { 0 } else { 4 }
if ($referenceMsaa -ne $actualMsaa -and -not $DiagnosticComparison) { throw 'MSAA differs; this is not a parity comparison.' }
if ($referenceManifest.profile -ne $actualManifest.profile -or
    $referenceManifest.frame -ne $actualManifest.frame -or
    $referenceManifest.seed -ne $actualManifest.seed -or
    $referenceManifest.pilot -ne $actualManifest.pilot -or
    (Get-Content -LiteralPath (Join-Path $referencePath 'settings.json') -Raw) -cne
    (Get-Content -LiteralPath (Join-Path $actualPath 'settings.json') -Raw)) {
    throw 'Capture profiles, frame, pilot or registry settings differ; comparison invalid.'
}
if ($referenceManifest.results.Count -ne $actualManifest.results.Count) { throw 'Corpus sizes differ.' }
if ([bool]$referenceManifest.layer_probe -ne [bool]$actualManifest.layer_probe -and -not $DiagnosticComparison) {
    throw 'Layer fixture differs; use DiagnosticComparison for intentional composition changes.'
}
if ([bool]$referenceManifest.reticle_probe -ne [bool]$actualManifest.reticle_probe -and -not $DiagnosticComparison) {
    throw 'Reticle fixture differs; use DiagnosticComparison for intentional HUD changes.'
}
if ([bool]$referenceManifest.reticle_reference -ne [bool]$actualManifest.reticle_reference -and -not $DiagnosticComparison) {
    throw 'Reticle executor differs; use DiagnosticComparison for the legacy reference check.'
}
if ([bool]$referenceManifest.observation_probe -ne [bool]$actualManifest.observation_probe -and -not $DiagnosticComparison) {
    throw 'Observation inspection differs; use DiagnosticComparison for non-mutating query verification.'
}
if ([bool]$referenceManifest.motion_blur -ne [bool]$actualManifest.motion_blur -and -not $DiagnosticComparison) {
    throw 'Motion blur differs; use DiagnosticComparison for intentional processing changes.'
}
if ([bool]$referenceManifest.scorch_probe -ne [bool]$actualManifest.scorch_probe -and -not $DiagnosticComparison) {
    throw 'Scorch fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.scorch_view_probe -ne [bool]$actualManifest.scorch_view_probe -and -not $DiagnosticComparison) {
    throw 'Scorch camera differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.atmosphere_probe -ne [bool]$actualManifest.atmosphere_probe -and -not $DiagnosticComparison) {
    throw 'Atmosphere fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.axis_probe -ne [bool]$actualManifest.axis_probe -and -not $DiagnosticComparison) {
    throw 'Axis fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.ring_probe -ne [bool]$actualManifest.ring_probe -and -not $DiagnosticComparison) {
    throw 'Ring fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.mirror_camera_probe -ne [bool]$actualManifest.mirror_camera_probe -and -not $DiagnosticComparison) {
    throw 'Mirror camera fixture mismatch requires DiagnosticComparison.'
}
if ([bool]$referenceManifest.view_image_probe -ne [bool]$actualManifest.view_image_probe -and -not $DiagnosticComparison) {
    throw 'View image fixture mismatch requires DiagnosticComparison.'
}
if ([int]$referenceManifest.inspection_banner_revision -ne [int]$actualManifest.inspection_banner_revision -and -not $DiagnosticComparison) {
    throw 'Inspection banner composition changed; use DiagnosticComparison for intentional fixture changes.'
}
if ([int]$referenceManifest.view_image_revision -ne [int]$actualManifest.view_image_revision -and -not $DiagnosticComparison) {
    throw 'View image graph fixture changed; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.depth_policy_probe -ne [bool]$actualManifest.depth_policy_probe -and -not $DiagnosticComparison) {
    throw 'Depth policy fixtures differ; this is not a parity comparison.'
}
if ([int]$referenceManifest.ring_probe_revision -ne [int]$actualManifest.ring_probe_revision -and -not $DiagnosticComparison) {
    throw 'Ring fixture revisions differ; this is not a parity comparison.'
}
if ([bool]$referenceManifest.model_effects_probe -ne [bool]$actualManifest.model_effects_probe -and -not $DiagnosticComparison) {
    throw 'Model effects fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.standalone_model_probe -ne [bool]$actualManifest.standalone_model_probe -and -not $DiagnosticComparison) {
    throw 'Standalone model ownership differs; this is not a parity comparison.'
}
if ([bool]$referenceManifest.display_view_probe -ne [bool]$actualManifest.display_view_probe -and -not $DiagnosticComparison) {
    throw 'Display-view fixture mismatch requires DiagnosticComparison.'
}
if ([bool]$referenceManifest.cockpit_probe -ne [bool]$actualManifest.cockpit_probe -and -not $DiagnosticComparison) {
    throw 'Cockpit fixture mismatch requires DiagnosticComparison.'
}
if ([int]$referenceManifest.cockpit_probe_revision -ne [int]$actualManifest.cockpit_probe_revision -and -not $DiagnosticComparison) {
    throw 'Cockpit fixture revision mismatch requires DiagnosticComparison.'
}
if ([bool]$referenceManifest.model_collection_probe -ne [bool]$actualManifest.model_collection_probe -and -not $DiagnosticComparison) {
    throw 'Model content collections differ; this is not a parity comparison.'
}
if ([int]$referenceManifest.model_effects_revision -ne [int]$actualManifest.model_effects_revision -and -not $DiagnosticComparison) {
    throw 'Model effects fixture revision differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.satellite_probe -ne [bool]$actualManifest.satellite_probe -and -not $DiagnosticComparison) {
    throw 'Satellite probe mismatch requires DiagnosticComparison.'
}
if ([bool]$referenceManifest.sky_probe -ne [bool]$actualManifest.sky_probe -and -not $DiagnosticComparison) {
    throw 'Sky probe differs; use -DiagnosticComparison only for intentional cross-fixture comparisons.'
}
if ([bool]$referenceManifest.history_probe -ne [bool]$actualManifest.history_probe -and -not $DiagnosticComparison) {
    throw 'History-probe fixtures differ; this is not a parity comparison.'
}
$referenceLodTier = if ($null -eq $referenceManifest.lod_probe_tier) { -1 } else { [int]$referenceManifest.lod_probe_tier }
$actualLodTier = if ($null -eq $actualManifest.lod_probe_tier) { -1 } else { [int]$actualManifest.lod_probe_tier }
if ($referenceLodTier -ne $actualLodTier -and -not $DiagnosticComparison) {
    throw 'Model LOD tiers differ; this is not a parity comparison.'
}
if ([int]$referenceManifest.lod_probe_revision -ne [int]$actualManifest.lod_probe_revision -and -not $DiagnosticComparison) {
    throw 'Model LOD fixture revisions differ; this is not a parity comparison.'
}
if ([bool]$referenceManifest.resource_probe -ne [bool]$actualManifest.resource_probe -and -not $DiagnosticComparison) {
    throw 'Resource transition fixtures differ; this is not a parity comparison.'
}
if ([bool]$referenceManifest.terrain_probe -ne [bool]$actualManifest.terrain_probe -and -not $DiagnosticComparison) {
    throw 'Terrain fixture differs; use -DiagnosticComparison, never treat this as parity acceptance.'
}
if ([bool]$referenceManifest.vertex_lighting -ne [bool]$actualManifest.vertex_lighting -and -not $DiagnosticComparison) {
    throw 'Lighting quality differs; use -DiagnosticComparison, never treat this as parity acceptance.'
}
if ([bool]$referenceManifest.deformation_probe -ne [bool]$actualManifest.deformation_probe -and -not $DiagnosticComparison) {
    throw 'Deformation fixture differs; use -DiagnosticComparison, never treat this as parity acceptance.'
}
if ([bool]$referenceManifest.scene_inspection -ne [bool]$actualManifest.scene_inspection -and -not $DiagnosticComparison) {
    throw 'Inspection and complete-renderer captures differ in scope; use -DiagnosticComparison, never treat this as parity acceptance.'
}
$results = foreach ($entry in $referenceManifest.results) {
    $other = @($actualManifest.results | Where-Object save -eq $entry.save)
    if ($other.Count -ne 1 -or -not $entry.success -or -not $other[0].success -or $entry.save_hash -ne $other[0].save_hash) {
        throw "Missing, failed or mismatched save: $($entry.save)"
    }
    $metrics = [RendererImageComparison]::Measure((Join-Path $referencePath "$($entry.save).png"),
        (Join-Path $actualPath "$($entry.save).png"),$sampleRegion)
    [pscustomobject]@{save=$entry.save; diagnostic_comparison=[bool]$DiagnosticComparison; width=$metrics[0]; height=$metrics[1]; mean_rgb_error=$metrics[2];
        region=$sampleRegion; changed_percent=$metrics[3]; above_two_percent=$metrics[4]; max_channel_error=$metrics[5];
        above_two_bounds=@($metrics[6],$metrics[7],$metrics[8],$metrics[9])}
}
if (Test-Path -LiteralPath $Report) { throw 'Report already exists.' }
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Report -Encoding UTF8
$results | Format-Table -AutoSize
# Deliberately no automatic visual pass/fail threshold. First measure repeatability,
# then inspect material differences and temporal behavior against that noise floor.
