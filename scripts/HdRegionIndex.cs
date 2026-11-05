// Installer-owned binary index for the data-driven native HD region loader.
// PNGs remain the authoritative artwork and are never resized/re-encoded here.
using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
namespace WoBeta {
 public sealed class HdRegionIndex {
  readonly MemoryStream records=new MemoryStream();
  int count;
  public int Count {get{return count;}}
  public void Add(string png,string native,int page,int x,int y,int width,int height,bool alpha) {
   if(!Regex.IsMatch(png,@"\A[a-z0-9-]+\.png\z",RegexOptions.IgnoreCase) || png.Length>=128 ||
      !Regex.IsMatch(native,@"\A[a-z0-9]+\.tga\z",RegexOptions.IgnoreCase) || native.Length>=32 ||
      page<0 || page>255 || x<0 || y<0 || width<1 || height<1 || x>256-width || y>256-height || count>=16384)
    throw new InvalidDataException("Invalid HD texture mapping: "+png);
   var writer=new BinaryWriter(records,Encoding.UTF8,true);
   byte[] name=new byte[256],atlas=new byte[32];
   Encoding.Unicode.GetBytes(png).CopyTo(name,0);Encoding.ASCII.GetBytes(native).CopyTo(atlas,0);
   writer.Write(name);writer.Write(atlas);
   foreach(int value in new int[]{page,x,y,width,height,alpha?1:0})writer.Write(value);
   writer.Flush();count++;
  }
  public bool Save(string path) {
   byte[] data;
   using(var buffer=new MemoryStream())using(var writer=new BinaryWriter(buffer)){
    writer.Write(Encoding.ASCII.GetBytes("WOHDREG1"));writer.Write(count);writer.Write(records.ToArray());
    writer.Flush();data=buffer.ToArray();
   }
   if(File.Exists(path)){
    byte[] old=File.ReadAllBytes(path);bool same=old.Length==data.Length;
    for(int i=0;same && i<data.Length;i++)same=old[i]==data[i];
    if(same)return false;
   }
   Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
   string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
   try{File.WriteAllBytes(temporary,data);if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}
   finally{if(File.Exists(temporary))File.Delete(temporary);}
   return true;
  }
  public static bool HasTransparency(Bitmap image) {
   // Indexed transparency and explicit RGBA both pass through this conversion.
   if(!Image.IsAlphaPixelFormat(image.PixelFormat) && (image.Flags&2)==0)return false;
   var area=new Rectangle(0,0,image.Width,image.Height);
   var bits=image.LockBits(area,ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
   try{
    byte[] row=new byte[image.Width*4];
    for(int y=0;y<image.Height;y++){
     Marshal.Copy(IntPtr.Add(bits.Scan0,y*bits.Stride),row,0,row.Length);
     for(int x=3;x<row.Length;x+=4)if(row[x]!=255)return true;
    }
    return false;
   }finally{image.UnlockBits(bits);}
  }
 }
}
