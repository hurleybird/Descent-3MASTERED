# Exact native screenshot qualification, including the shipped uncompressed TGA
# writer's 5-bit channels. No tolerance, crop, orientation or alpha exceptions.
function Test-RendererExportImage {
    param([string]$Reference,[string]$Export,[int]$Scale=1,[bool]$Tga=$false,[bool]$Upload1555=$false)
    if (!("RendererExportImageCheck" -as [type])) {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $drawingTypes = @([Drawing.Bitmap],[Drawing.Color]) + [Drawing.Bitmap].GetInterfaces()
        $drawingReferences = $drawingTypes | ForEach-Object {$_.Assembly.Location} | Select-Object -Unique
        Add-Type -ErrorAction Stop -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Drawing;
using System.IO;
public static class RendererExportImageCheck {
    public static long[] Check(string reference, string exported, int scale, bool tga, bool upload1555) {
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
                long mismatch = 0, transparent = 0;
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
                    if (actual.ToArgb() != expected.ToArgb()) ++mismatch;
                    if (actual.A != 255) ++transparent;
                }
                return new long[] {width,height,mismatch,transparent};
            } finally { if (png != null) png.Dispose(); }
        }
    }
}
'@
    }
    $result = [RendererExportImageCheck]::Check($Reference,$Export,$Scale,$Tga,$Upload1555)
    [ordered]@{width=$result[0]; height=$result[1]; mismatches=$result[2]; transparent=$result[3]}
}
