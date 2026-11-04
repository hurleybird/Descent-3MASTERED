# Exact native screenshot qualification, including the shipped uncompressed TGA
# writer's 5-bit channels. No tolerance, orientation or alpha exceptions.
# Subtitle cases separately require their intentional bottom text overlay.
function Test-RendererDisplayAlphaImage {
    param([string]$Image,[double]$Gamma)
    if ($Gamma -le 0) { throw 'Invalid image transfer gamma' }
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    $bitmap = [Drawing.Bitmap]::new($Image)
    try {
        $left = $bitmap.GetPixel(80,90); $right = $bitmap.GetPixel(240,90)
        $white = [math]::Round(255*[math]::Pow(.25,1/$Gamma))
        $red = [math]::Round(255*[math]::Pow(.125,1/$Gamma))
        $blue = [math]::Round(255*[math]::Pow(.25+.75*[math]::Pow(64.0/255,$Gamma),1/$Gamma))
        $actual = @($left.R,$left.G,$left.B,$left.A,$right.R,$right.G,$right.B,$right.A)
        $expected = @($white,$white,$blue,255,$red,0,64,255)
        for ($i=0; $i -lt $actual.Count; ++$i) {
            if ([math]::Abs($actual[$i]-$expected[$i]) -gt 2) {
                throw "Display image transfer failed channel $i : $($actual[$i]) expected $($expected[$i]) at gamma $Gamma"
            }
        }
        [ordered]@{gamma=$Gamma; rgba=$actual; expected=$expected}
    } finally { $bitmap.Dispose() }
}

function Test-RendererExportImage {
    param([string]$Reference,[string]$Export,[int]$Scale=1,[bool]$Tga=$false,[bool]$Upload1555=$false,[int]$SubtitleTop=-1)
    if (!("RendererExportImageCheck" -as [type])) {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $drawingTypes = @([Drawing.Bitmap],[Drawing.Color]) + [Drawing.Bitmap].GetInterfaces()
        $drawingReferences = $drawingTypes | ForEach-Object {$_.Assembly.Location} | Select-Object -Unique
        Add-Type -ErrorAction Stop -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Drawing;
using System.IO;
public static class RendererExportImageCheck {
    public static long[] Check(string reference, string exported, int scale, bool tga, bool upload1555, int subtitleTop) {
        if (scale != 1 && scale != 2) throw new ArgumentException("Invalid export scale");
        using (var source = new Bitmap(reference)) {
            Bitmap png = null; byte[] bytes = null;
            int width, height, offset = 0;
            try {
                if (tga) {
                    bytes = File.ReadAllBytes(exported);
                    if (bytes.Length < 18 || bytes[1] != 0 || bytes[2] != 2 || bytes[16] != 32 || bytes[17] != 40)
                        throw new InvalidDataException("Not the native top-left RGBA TGA format");
                    width = bytes[12]+256*bytes[13]; height = bytes[14]+256*bytes[15]; offset = 18+bytes[0];
                    if (bytes.LongLength != offset+(long)width*height*4) throw new InvalidDataException("TGA byte extent mismatch");
                } else { png = new Bitmap(exported); width = png.Width; height = png.Height; }
                if (width != source.Width*scale || height != source.Height*scale)
                    throw new InvalidDataException("Export extent mismatch");
                if (subtitleTop != -1 && (!upload1555 || tga || scale != 1 || subtitleTop < height/2 || subtitleTop >= height))
                    throw new InvalidDataException("Invalid intentional subtitle region");
                long mismatch = 0, transparent = 0, subtitleChanged = 0;
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    Color expected = source.GetPixel(x/scale,y/scale);
                    // Native source PNG bit-replicates 5-bit channels; the
                    // current RGBA8 bitmap uploader truncates 255*(n/31).
                    // gl_init disables packed pixel uploads. No tolerance.
                    if (upload1555) expected = Color.FromArgb(255,
                        ((expected.R>>3)*255)/31,((expected.G>>3)*255)/31,((expected.B>>3)*255)/31);
                    Color actual;
                    if (tga) {
                        int i = offset+(y*width+x)*4;
                        actual = Color.FromArgb(bytes[i+3],bytes[i+2],bytes[i+1],bytes[i]);
                        expected = Color.FromArgb(255,expected.R&248,expected.G&248,expected.B&248);
                    } else actual = png.GetPixel(x,y);
                    if (actual.ToArgb() != expected.ToArgb()) {
                        if (subtitleTop >= 0 && y >= subtitleTop) ++subtitleChanged;
                        else ++mismatch;
                    }
                    if (actual.A != 255) ++transparent;
                }
                return new long[] {width,height,mismatch,transparent,subtitleChanged};
            } finally { if (png != null) png.Dispose(); }
        }
    }
}
'@
    }
    $result = [RendererExportImageCheck]::Check($Reference,$Export,$Scale,$Tga,$Upload1555,$SubtitleTop)
    [ordered]@{width=$result[0]; height=$result[1]; mismatches=$result[2]; transparent=$result[3]; intentional_subtitle_changes=$result[4]}
}
