# Shared Windows PowerShell5.1 installer primitives. No downloaded executables are run.
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$script:ModRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:ModPowerShell=Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell/v1.0/powershell.exe'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.Drawing
if (-not ('WoBeta.Files' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace WoBeta {
 public static class Files {
  public static string Hash(byte[] data) { using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(data)).Replace("-","").ToLowerInvariant(); }
  public static string HashFile(string path) {using(var f=File.OpenRead(path))using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(f)).Replace("-","").ToLowerInvariant();}
  public static void Parent(string path) {Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));}
  public static byte[] Read(Stream f,int n) {byte[] b=new byte[n];int at=0,k;while(at<n && (k=f.Read(b,at,n-at))>0)at+=k;if(at!=n)throw new InvalidDataException("Truncated input");return b;}
  // Source/target hashes guard the full binary, not only changed regions.
  public static string Delta(string source,string target,string patch) {
   byte[] a=File.ReadAllBytes(source),b=File.ReadAllBytes(target);Parent(patch);
   using(var f=File.Create(patch))using(var z=new GZipStream(f,CompressionMode.Compress))using(var w=new BinaryWriter(z)) {
    w.Write(Encoding.ASCII.GetBytes("WODIFF1\0"));w.Write(a.Length);w.Write(b.Length);
    w.Write(Encoding.ASCII.GetBytes(Hash(a)));w.Write(Encoding.ASCII.GetBytes(Hash(b)));
    int at=0;while(at<b.Length) {while(at<b.Length && b[at]==(at<a.Length?a[at]:(byte)0))at++;if(at==b.Length)break;
     int start=at,last=at;while(at<b.Length) {if(b[at]!=(at<a.Length?a[at]:(byte)0))last=at;if(at-last>16)break;at++;}
     int n=last-start+1;byte[] xor=new byte[n];for(int i=0;i<n;i++)xor[i]=(byte)(b[start+i]^(start+i<a.Length?a[start+i]:(byte)0));
     w.Write(start);w.Write(n);w.Write(xor);at=last+1;
    }w.Write(-1);
   }return Hash(b);
  }
  public static void Apply(string source,string patch,string target) {
   byte[] a=File.ReadAllBytes(source),output;string expected;
   using(var f=File.OpenRead(patch))using(var z=new GZipStream(f,CompressionMode.Decompress))using(var r=new BinaryReader(z)) {
    if(Encoding.ASCII.GetString(r.ReadBytes(8))!="WODIFF1\0")throw new InvalidDataException("Unknown patch format");
    int n=r.ReadInt32(),m=r.ReadInt32();string before=Encoding.ASCII.GetString(r.ReadBytes(64));expected=Encoding.ASCII.GetString(r.ReadBytes(64));
    if(n!=a.Length || Hash(a)!=before)throw new InvalidDataException("Unsupported source checksum: "+Path.GetFileName(source));
    if(m<0 || m>128*1024*1024)throw new InvalidDataException("Invalid patch size");output=new byte[m];Array.Copy(a,output,Math.Min(a.Length,m));
    int end=0;while(true) {int at=r.ReadInt32();if(at==-1)break;int count=r.ReadInt32();if(at<end || count<=0 || at>m-count)throw new InvalidDataException("Invalid patch range");
     byte[] bytes=r.ReadBytes(count);if(bytes.Length!=count)throw new InvalidDataException("Truncated patch");for(int i=0;i<count;i++)output[at+i]^=bytes[i];end=at+count;
    }if(r.Read()!=-1)throw new InvalidDataException("Trailing patch data");
   }if(Hash(output)!=expected)throw new InvalidDataException("Patched output checksum failed");Parent(target);File.WriteAllBytes(target,output);
  }
  public static void Audio(string input,long offset,long sectors,int stride,string output) {
   if(sectors<=0 || stride<2352 || sectors>uint.MaxValue/2352)throw new InvalidDataException("Invalid audio extent");
   using(var f=File.OpenRead(input)) {
    if(offset<0 || offset+sectors*stride>f.Length)throw new InvalidDataException("Audio exceeds image");
    f.Position=offset+Math.Min(750,sectors/2)*stride;byte[] probe=Read(f,(int)Math.Min(75,sectors/2)*stride);
    long little=0,big=0;for(int i=4;i+1<probe.Length;i+=2) {little+=Math.Abs((int)BitConverter.ToInt16(probe,i)-BitConverter.ToInt16(probe,i-4));big+=Math.Abs((int)(short)((probe[i]<<8)|probe[i+1])-(short)((probe[i-4]<<8)|probe[i-3]));}
    if(Math.Max(little,big)<Math.Min(little,big)*1.5)throw new InvalidDataException("Ambiguous CD audio byte order");bool swap=big<little;
    Parent(output);using(var w=new BinaryWriter(File.Create(output))) {
     uint bytes=(uint)(sectors*2352);w.Write(Encoding.ASCII.GetBytes("RIFF"));w.Write(bytes+36);w.Write(Encoding.ASCII.GetBytes("WAVEfmt "));w.Write(16);w.Write((short)1);w.Write((short)2);w.Write(44100);w.Write(176400);w.Write((short)4);w.Write((short)16);w.Write(Encoding.ASCII.GetBytes("data"));w.Write(bytes);
     f.Position=offset;long left=sectors;
     while(left>0) {int take=(int)Math.Min(left,1024);byte[] raw=Read(f,take*stride);if(stride==2352) {if(swap)for(int i=0;i<raw.Length;i+=2){byte t=raw[i];raw[i]=raw[i+1];raw[i+1]=t;}w.Write(raw);}
      else for(int s=0;s<take;s++){if(swap)for(int i=s*stride;i<s*stride+2352;i+=2){byte t=raw[i];raw[i]=raw[i+1];raw[i+1]=t;}w.Write(raw,s*stride,2352);}left-=take;
     }
    }
   }
  }
  public static byte[] Pixels(string input,out int width,out int height) {
   using(var b=new Bitmap(input)) {
    width=b.Width;height=b.Height;var d=b.LockBits(new Rectangle(0,0,width,height),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);byte[] pixels=new byte[width*height*4];
    try {for(int y=0;y<height;y++)Marshal.Copy(IntPtr.Add(d.Scan0,y*d.Stride),pixels,y*width*4,width*4);}finally{b.UnlockBits(d);}return pixels;
   }
  }
  public static void PackUi(string input,string output) {
   int w,h;byte[] p=Pixels(input,out w,out h);if(w!=h || w<256 || w>4096 || (w&(w-1))!=0)throw new InvalidDataException("UI atlas must be square, power of2,256..4096");
   Parent(output);using(var f=new BinaryWriter(File.Create(output))){f.Write(Encoding.ASCII.GetBytes("WUI1"));f.Write(w);f.Write(h);f.Write(p.Length);f.Write(p);}
  }
  public static void PackArt(string input,string output) {
   int w,h;byte[] p=Pixels(input,out w,out h);if(w<256 || h<256 || w>16384 || h>8192)throw new InvalidDataException("Invalid artwork size");
   Parent(output);using(var f=new BinaryWriter(File.Create(output))) {f.Write((byte)'B');f.Write((byte)'M');f.Write(54+p.Length);f.Write(0);f.Write(54);f.Write(40);f.Write(w);f.Write(-h);f.Write((short)1);f.Write((short)32);f.Write(0);f.Write(p.Length);f.Write(new byte[16]);f.Write(p);}
  }
  public static void Png(byte[] pixels,int w,int h,string path) {
   Parent(path);using(var b=new Bitmap(w,h,PixelFormat.Format32bppArgb)) {var d=b.LockBits(new Rectangle(0,0,w,h),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
    try{for(int y=0;y<h;y++)Marshal.Copy(pixels,y*w*4,IntPtr.Add(d.Scan0,y*d.Stride),w*4);}finally{b.UnlockBits(d);}b.Save(path,ImageFormat.Png);}
  }
  static void Color555(ushort c,byte[] p,int at,bool opaque){p[at]=(byte)(((c>>10)&31)*255/31);p[at+1]=(byte)(((c>>5)&31)*255/31);p[at+2]=(byte)((c&31)*255/31);p[at+3]=(byte)(opaque||c!=0?255:0);}
  public static void Tga(string source,string dest) {
   byte[] b=File.ReadAllBytes(source);if(b.Length<18 || b[2]!=2 || b[16]!=16)throw new InvalidDataException("Expected native RGB555 TGA");int w=BitConverter.ToUInt16(b,12),h=BitConverter.ToUInt16(b,14),start=18+b[0];
   if(w<1 || h<1 || w*h>16777216 || start+(long)w*h*2>b.Length)throw new InvalidDataException("Bad TGA extent");byte[] p=new byte[w*h*4];
   // The PC loader ignores TGA origin bits: retain its raw row order.
   for(int i=0;i<w*h;i++) {ushort c=BitConverter.ToUInt16(b,start+i*2);p[i*4]=(byte)((c&31)*255/31);p[i*4+1]=(byte)(((c>>5)&31)*255/31);p[i*4+2]=(byte)(((c>>10)&31)*255/31);p[i*4+3]=(byte)((c&32767)!=0?255:0);}Png(p,w,h,dest);
  }
  public static void Tim(byte[] b,string dest) {
   if(b.Length<20 || BitConverter.ToUInt32(b,0)!=16)throw new InvalidDataException("Bad TIM");int mode=(int)BitConverter.ToUInt32(b,4),at=8;ushort[] palette=null;
   if((mode&8)!=0) {int n=BitConverter.ToInt32(b,at);if(n<12 || n>b.Length-at)throw new InvalidDataException("Bad CLUT");int colors=(n-12)/2;palette=new ushort[colors];for(int i=0;i<colors;i++)palette[i]=BitConverter.ToUInt16(b,at+12+i*2);at+=n;}
   int block=BitConverter.ToInt32(b,at),words=BitConverter.ToUInt16(b,at+8),h=BitConverter.ToUInt16(b,at+10);mode&=7;int w=mode==0?words*4:mode==1?words*2:mode==2?words:words*2/3;
   if(block<12 || at+block>b.Length || w<1 || h<1 || w*h>16777216 || words*(long)h*2>block-12)throw new InvalidDataException("Bad TIM pixels");byte[] p=new byte[w*h*4];int start=at+12;
   for(int y=0;y<h;y++)for(int x=0;x<w;x++) {int o=(y*w+x)*4,row=start+y*words*2;
    if(mode==3){p[o]=b[row+x*3+2];p[o+1]=b[row+x*3+1];p[o+2]=b[row+x*3];p[o+3]=255;}
    else {ushort c;if(mode==2)c=BitConverter.ToUInt16(b,row+x*2);else {if(palette==null)throw new InvalidDataException("TIM needs CLUT");int index=mode==0?(b[row+x/2]>>((x&1)*4))&15:b[row+x];if(index>=palette.Length)throw new InvalidDataException("CLUT index");c=palette[index];}Color555(c,p,o,false);}
   }Png(p,w,h,dest);
  }
  sealed class Bits {byte[] data;int bit;public Bits(byte[] b,int offset){data=b;bit=offset*8;}public int Get(int n){int v=0;for(int i=0;i<n;i++){if(bit/8>=data.Length)throw new InvalidDataException("Truncated LZ stream");v=(v<<1)|((data[bit/8]>>(7-(bit&7)))&1);bit++;}return v;}}
  public static void Cmp(string source,string dest) {
   byte[] b=File.ReadAllBytes(source);int count=BitConverter.ToInt32(b,0);if(count<1 || count>4096 || 4+count*4>b.Length)throw new InvalidDataException("Bad CMP index");int total=0;int[] sizes=new int[count];for(int i=0;i<count;i++){sizes[i]=BitConverter.ToInt32(b,4+i*4);if(sizes[i]<1 || sizes[i]>16777216 || total>67108864-sizes[i])throw new InvalidDataException("Bad CMP size");total+=sizes[i];}
   byte[] decoded=new byte[total],window=new byte[8192];int at=0,pos=1;var bits=new Bits(b,4+count*4);
   while(true) {if(bits.Get(1)!=0){if(at==total)throw new InvalidDataException("CMP overflow");byte c=(byte)bits.Get(8);decoded[at++]=c;window[pos]=c;pos=(pos+1)&8191;}
    else {int from=bits.Get(13);if(from==0)break;int n=bits.Get(4)+3;if(at>total-n)throw new InvalidDataException("CMP match overflow");for(int i=0;i<n;i++){byte c=window[(from+i)&8191];decoded[at++]=c;window[pos]=c;pos=(pos+1)&8191;}}
   }if(at!=total)throw new InvalidDataException("Incomplete CMP");Directory.CreateDirectory(dest);at=0;for(int i=0;i<count;i++){byte[] tim=new byte[sizes[i]];Array.Copy(decoded,at,tim,0,tim.Length);Tim(tim,Path.Combine(dest,i.ToString("D4")+".png"));at+=tim.Length;}
  }
 }
 public sealed class DiscFile {public string Path;public long Lba,Length;public bool Directory;}
 public sealed class Disc : IDisposable {
  FileStream stream;public int Stride,Payload;long origin;public List<DiscFile> Entries=new List<DiscFile>();
  public Disc(string path,long start) {stream=File.OpenRead(path);origin=start;bool found=false;
   foreach(int s in new int[]{2048,2352,2448})foreach(int p in new int[]{0,16,24}) {if(start+16L*s+p+2048>stream.Length)continue;stream.Position=start+16L*s+p;byte[] b=Files.Read(stream,2048);if(b[0]==1 && Encoding.ASCII.GetString(b,1,5)=="CD001"){Stride=s;Payload=p;found=true;break;}if(found)break;}
   if(!found){stream.Dispose();throw new InvalidDataException("No supported ISO9660 data track: "+Path.GetFileName(path));}byte[] pvd=Read(16,2048);Walk(BitConverter.ToUInt32(pvd,158),BitConverter.ToUInt32(pvd,166),"",0);
  }
  public byte[] Read(long lba,long size) {if(size<0 || size>67108864)throw new InvalidDataException("Oversize ISO record");byte[] b=new byte[(int)size];int at=0;while(at<b.Length){stream.Position=origin+lba*Stride+Payload;int n=Math.Min(2048,b.Length-at);byte[] r=Files.Read(stream,n);Array.Copy(r,0,b,at,n);at+=n;lba++;}return b;}
  void Walk(long lba,long size,string prefix,int depth) {if(depth>16 || Entries.Count>50000)throw new InvalidDataException("ISO directory limit");byte[] b=Read(lba,size);int at=0;while(at<b.Length){int n=b[at];if(n==0){at=(at/2048+1)*2048;continue;}if(n<34 || at+n>b.Length || b[at+32]>n-33)throw new InvalidDataException("Bad ISO directory");string name=Encoding.ASCII.GetString(b,at+33,b[at+32]).Split(';')[0];long sector=BitConverter.ToUInt32(b,at+2),len=BitConverter.ToUInt32(b,at+10);bool directory=(b[at+25]&2)!=0;at+=n;if(name=="\0"||name=="\u0001")continue;
    if(name.Contains("/") || name.Contains("\\") || name==".." || name.Contains(":"))throw new InvalidDataException("Unsafe ISO entry");string path=prefix+name;Entries.Add(new DiscFile{Path=path,Lba=sector,Length=len,Directory=directory});if(directory)Walk(sector,len,path+"/",depth+1);
   }}
  public void Extract(DiscFile e,string output) {if(e.Directory)return;Files.Parent(output);using(var f=File.Create(output)){long left=e.Length,lba=e.Lba;while(left>0){int n=(int)Math.Min(left,2048*128);byte[] b=Read(lba,n);f.Write(b,0,n);left-=n;lba+=(n+2047)/2048;}}}
  public void Dispose(){stream.Dispose();}
 }
}
'@
}
function Get-ModHash([string]$Path) { [WoBeta.Files]::HashFile([IO.Path]::GetFullPath($Path)) }
function Get-ModChild([string]$Root,[string]$Relative) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path leaves its root: $Relative" }
    return $path
}
function Write-ModJson([string]$Path,$Value) {
    [WoBeta.Files]::Parent($Path)
    $json = ConvertTo-Json -InputObject $Value -Depth 14
    [IO.File]::WriteAllText($Path,$json,(New-Object Text.UTF8Encoding($false)))
}
function Copy-ModTree([string]$Source,[string]$Destination) {
    $base=[IO.Path]::GetFullPath($Source).TrimEnd('\')
    foreach($file in Get-ChildItem -LiteralPath $base -File -Recurse) {
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked files are not supported as game sources.'}
        $target=Get-ModChild $Destination $file.FullName.Substring($base.Length+1)
        [WoBeta.Files]::Parent($target);[IO.File]::Copy($file.FullName,$target,$true)
    }
}
function Expand-ModZip([string]$Path,[string]$Destination) {
    $zip=[IO.Compression.ZipFile]::OpenRead($Path)
    try {
        [long]$total=0
        foreach($entry in $zip.Entries) {
            $total+=$entry.Length;if($total -gt 12GB){throw 'Archive exceeds the12GB extraction limit.'}
            $target=Get-ModChild $Destination $entry.FullName
            if([string]::IsNullOrEmpty($entry.Name)){[IO.Directory]::CreateDirectory($target)|Out-Null;continue}
            [WoBeta.Files]::Parent($target);$input=$entry.Open();$output=[IO.File]::Create($target)
            try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}
        }
    } finally {$zip.Dispose()}
}
function Test-ModGameFile([string]$Relative) {
    $r=$Relative.Replace('\','/')
    return $r -match '^(?i)(Common|Blitsurf|Pics|Sound|Textures|Track\d{2})/' -or $r -match '^(?i)(Wipeout2(?:_HighTest)?\.exe|Wipeout2097\.exe|D3D\.exe|CONFIG\.DAT|SAVEGAME\.BIN|Wipeout2097\.ini)$'
}
function Import-ModFolder([string]$Source,[string]$Destination) {
    $base=[IO.Path]::GetFullPath($Source).TrimEnd('\')
    foreach($file in Get-ChildItem -LiteralPath $base -File -Recurse) {
        $rel=$file.FullName.Substring($base.Length+1)
        if(Test-ModGameFile $rel){$to=Get-ModChild $Destination $rel;[WoBeta.Files]::Parent($to);[IO.File]::Copy($file.FullName,$to,$true)}
    }
}
function Import-ModDisc([string]$Image,[string]$Destination,[long]$Offset=0) {
    $disc=New-Object WoBeta.Disc($Image,$Offset)
    try { foreach($entry in $disc.Entries){if(-not $entry.Directory -and (Test-ModGameFile $entry.Path)){$disc.Extract($entry,(Get-ModChild $Destination $entry.Path))}} }finally{$disc.Dispose()}
}
