param(
    [Parameter(Mandatory=$true)][string]$Reference,
    [Parameter(Mandatory=$true)][string]$Actual,
    [Parameter(Mandatory=$true)][string]$Report,
    [switch]$DiagnosticComparison
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    $forwarded = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Reference',$Reference,'-Actual',$Actual,'-Report',$Report)
    if ($DiagnosticComparison) { $forwarded += '-DiagnosticComparison' }
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
    public static double[] Measure(string leftPath, string rightPath) {
        using (var left = new Bitmap(leftPath))
        using (var right = new Bitmap(rightPath)) {
            if (left.Size != right.Size) throw new Exception("Image dimensions differ");
            var rect = new Rectangle(0, 0, left.Width, left.Height);
            var a = left.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var b = right.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                var rowA = new byte[left.Width * 4];
                var rowB = new byte[left.Width * 4];
                long sum = 0, different = 0, aboveTwo = 0;
                int maximum = 0;
                int minX = left.Width, minY = left.Height, maxX = -1, maxY = -1;
                for (int y = 0; y < left.Height; ++y) {
                    Marshal.Copy(IntPtr.Add(a.Scan0, y*a.Stride), rowA, 0, rowA.Length);
                    Marshal.Copy(IntPtr.Add(b.Scan0, y*b.Stride), rowB, 0, rowB.Length);
                    for (int x = 0; x < rowA.Length; x += 4) {
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
                double pixels = (double)left.Width * left.Height;
                return new double[] {left.Width, left.Height, sum/(pixels*3),
                    different/pixels*100, aboveTwo/pixels*100, maximum,
                    maxX < 0 ? -1 : minX,maxX < 0 ? -1 : minY,maxX,maxY};
            } finally { left.UnlockBits(a); right.UnlockBits(b); }
        }
    }
}
'@
$referencePath = (Resolve-Path -LiteralPath $Reference).Path
$actualPath = (Resolve-Path -LiteralPath $Actual).Path
$referenceManifest = Get-Content -LiteralPath (Join-Path $referencePath 'manifest.json') -Raw | ConvertFrom-Json
$actualManifest = Get-Content -LiteralPath (Join-Path $actualPath 'manifest.json') -Raw | ConvertFrom-Json
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
if ([bool]$referenceManifest.atmosphere_probe -ne [bool]$actualManifest.atmosphere_probe -and -not $DiagnosticComparison) {
    throw 'Atmosphere fixture differs; use DiagnosticComparison for intentional fixture changes.'
}
if ([bool]$referenceManifest.axis_probe -ne [bool]$actualManifest.axis_probe -and -not $DiagnosticComparison) {
    throw 'Axis fixture differs; use DiagnosticComparison for intentional fixture changes.'
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
        (Join-Path $actualPath "$($entry.save).png"))
    [pscustomobject]@{save=$entry.save; diagnostic_comparison=[bool]$DiagnosticComparison; width=$metrics[0]; height=$metrics[1]; mean_rgb_error=$metrics[2];
        changed_percent=$metrics[3]; above_two_percent=$metrics[4]; max_channel_error=$metrics[5];
        above_two_bounds=@($metrics[6],$metrics[7],$metrics[8],$metrics[9])}
}
if (Test-Path -LiteralPath $Report) { throw 'Report already exists.' }
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Report -Encoding UTF8
$results | Format-Table -AutoSize
# Deliberately no automatic visual pass/fail threshold. First measure repeatability,
# then inspect material differences and temporal behavior against that noise floor.
