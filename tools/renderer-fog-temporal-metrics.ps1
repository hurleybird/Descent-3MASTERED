# Run with Windows PowerShell (System.Drawing). Input names follow the fog
# capture-series corpus convention documented in renderer-recovery-checkpoint.md.
# Pixel error is a diagnostic, not proof of perceptual equivalence.
param([Parameter(Mandatory=$true)][string]$Root,[string]$Scenario='stable',[int]$First=90,[int]$Last=95,[string]$Variant='', [switch]$Series,[switch]$BoundaryBand,[string[]]$Modes=@('raw','temporal'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class FogResidual {
    static byte[] Read(string path,out int w,out int h) {
        using (var b=new Bitmap(path)) {
            w=b.Width; h=b.Height;
            var d=b.LockBits(new Rectangle(0,0,w,h),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
            try {
                var p=new byte[w*h*4];
                for(int y=0;y<h;++y) Marshal.Copy(IntPtr.Add(d.Scan0,y*d.Stride),p,y*w*4,w*4);
                return p;
            } finally { b.UnlockBits(d); }
        }
    }
    static bool Boundary(byte[] p,int x,int y,int w,int h) {
        int low=255,high=0;
        // A reference-owned cross includes BOTH the clear and foggy
        // sides of the transition. HUD/geometry edges can also enter this mask.
        int radius=Math.Max(1,(int)Math.Round(4.0*h/720));
        for(int offset=-radius;offset<=radius;++offset) {
            int a=p[(y*w+Math.Max(0,Math.Min(w-1,x+offset)))*4];
            int b=p[(Math.Max(0,Math.Min(h-1,y+offset))*w+x)*4];
            low=Math.Min(low,Math.Min(a,b)); high=Math.Max(high,Math.Max(a,b));
        }
        return low==0 && high>0 && high<80;
    }
    public static double[] Measure(string a,string b,string c,string d,bool edge,bool boundary) {
        int w,h,w1,h1,w2,h2,w3,h3;
        var r0=Read(a,out w,out h); var r1=Read(b,out w1,out h1);
        var p0=Read(c,out w2,out h2); var p1=Read(d,out w3,out h3);
        if(w!=w1||w!=w2||w!=w3||h!=h1||h!=h2||h!=h3) throw new Exception("Size mismatch");
        double count=0,error=0,flicker=0,energy=0,above=0,maximum=0;
        // Keep the same angular region/band at different output resolutions.
        int firstRow=(int)Math.Round(100.0*h/720),lastRow=(int)Math.Round(430.0*h/720);
        for(int y=firstRow;y<Math.Min(lastRow,h);++y) for(int x=0;x<w;++x) {
            int i=(y*w+x)*4;
            // Low optical depth includes the hard fog/clear transition and
            // excludes fully clear space. This mask is reference-owned.
            if(edge && (boundary ? !(Boundary(r0,x,y,w,h)||Boundary(r1,x,y,w,h)) :
                !((r0[i]>0&&r0[i]<80)||(r1[i]>0&&r1[i]<80)))) continue;
            int peak=0;
            for(int k=0;k<3;++k) {
                int current=p1[i+k]-r1[i+k];
                int residual=current-(p0[i+k]-r0[i+k]);
                error+=Math.Abs(current); flicker+=Math.Abs(residual); energy+=residual*residual;
                peak=Math.Max(peak,Math.Abs(residual));
            }
            ++count; if(peak>2) ++above; maximum=Math.Max(maximum,peak);
        }
        return new double[]{count,error/(count*3),flicker/(count*3),Math.Sqrt(energy/(count*3)),100*above/count,maximum};
    }
}
'@
function ImagePath([string]$mode,[int]$frame,[string]$variant) {
    if ($Series) {
        $suffix=if($frame -eq $Last) { '' } else { '.frame{0:d6}.png' -f $frame }
        return "$root\fog-series-$variant$Scenario-$mode\saveg003.png$suffix"
    }
    return "$root\fog-$variant$Scenario-$mode-$frame\saveg003.png"
}
foreach($mode in $Modes) {
    foreach($edge in @($false,$true)) {
        for($frame=$First+1;$frame -le $Last;++$frame) {
            $previous=$frame-1
            $v=[FogResidual]::Measure((ImagePath 'ref' $previous ''),(ImagePath 'ref' $frame ''),(ImagePath $mode $previous $Variant),(ImagePath $mode $frame $Variant),$edge,[bool]$BoundaryBand)
            [pscustomobject]@{scenario=$Scenario;mode=$mode;variant=$Variant;edge=$edge;boundary_band=[bool]$BoundaryBand;frame=$frame;pixels=$v[0];mean_error=$v[1];residual_flicker=$v[2];flicker_rms=$v[3];above2_percent=$v[4];maximum=$v[5]}
        }
    }
}
