// Native asset decoding for the fixed, maintainer-audited selection map.
// No PC/PS1 comparisons, hashes, quality scores or image processing heuristics.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
namespace WoBeta {
 public sealed class NativePixels {public int Width,Height;public ushort[] Pixels;}
 public sealed class TextureSources {
  readonly string game,ps1;
  readonly Dictionary<string,byte[]> files=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
  readonly Dictionary<string,byte[][]> sequences=new Dictionary<string,byte[][]>(StringComparer.OrdinalIgnoreCase);
  readonly Dictionary<string,Dictionary<string,byte[]>> wads=new Dictionary<string,Dictionary<string,byte[]>>(StringComparer.OrdinalIgnoreCase);
  public TextureSources(string gameRoot,string ps1Root){game=Path.GetFullPath(gameRoot);ps1=Path.GetFullPath(ps1Root);}
  public static void Commit(string temporary,string target){File.Replace(temporary,target,null);}
  static int U16(byte[] b,int p){if(p<0 || p>b.Length-2)throw new InvalidDataException("Truncated native texture");return b[p]|b[p+1]<<8;}
  static int U32(byte[] b,int p){if(p<0 || p>b.Length-4)throw new InvalidDataException("Truncated native texture");return BitConverter.ToInt32(b,p);}
  public static string Child(string root,string relative){string b=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);string p=Path.GetFullPath(Path.Combine(b,relative));if(!p.StartsWith(b+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Texture path leaves its root");return p;}
  public static Dictionary<string,byte[]> Wad(byte[] b){
   int n=U16(b,0),at=2;var names=new List<string>();var sizes=new List<int>();
   if(n<1 || n>256)throw new InvalidDataException("Invalid WAD directory count");
   for(int i=0;i<n;i++){
    int end=Array.IndexOf(b,(byte)0,at,Math.Min(64,b.Length-at));if(end<at)throw new InvalidDataException("Invalid WAD filename");
    string name=Encoding.ASCII.GetString(b,at,end-at);at=end+1;bool found=false;
    for(int p=at+4;p<=at+12 && p<=b.Length-9;p++){
     int size=U32(b,p);if(size>0 && size<=b.Length && size==U32(b,p+4) && b[p+8]==0){names.Add(name);sizes.Add(size);at=p+9;found=true;break;}
    }
    if(!found)throw new InvalidDataException("Invalid WAD directory record");
   }
   long total=0;foreach(int size in sizes)total+=size;
   if(total!=b.Length-at)throw new InvalidDataException("Invalid WAD payload extent");
   var result=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
   for(int i=0;i<n;i++){byte[] data=new byte[sizes[i]];Array.Copy(b,at,data,0,data.Length);result.Add(names[i],data);at+=data.Length;}return result;
  }
  sealed class Bits {
   readonly byte[] data;int bit;
   public Bits(byte[] b,int start){data=b;bit=start*8;}
   public int Get(int n){int v=0;for(int i=0;i<n;i++){if(bit/8>=data.Length)throw new InvalidDataException("Truncated CMP stream");v=v<<1|((data[bit/8]>>(7-(bit&7)))&1);bit++;}return v;}
  }
  public static byte[][] Cmp(byte[] b){
   int count=U32(b,0);if(count<1 || count>4096 || count>(b.Length-4)/4)throw new InvalidDataException("Invalid CMP directory");
   int total=0;int[] sizes=new int[count];
   for(int i=0;i<count;i++){int size=U32(b,4+i*4);if(size<0 || size>16777216 || total>67108864-size)throw new InvalidDataException("Invalid CMP entry size");sizes[i]=size;total+=size;}
   byte[] output=new byte[total],window=new byte[8192];int at=0,pos=1;var bits=new Bits(b,4+count*4);
   for(;;){
    if(bits.Get(1)!=0){if(at>=total)throw new InvalidDataException("CMP output overflow");byte value=(byte)bits.Get(8);output[at++]=value;window[pos]=value;pos=(pos+1)&8191;}
    else {int start=bits.Get(13);if(start==0)break;int length=bits.Get(4)+3;if(length>total-at)throw new InvalidDataException("CMP match overflow");for(int i=0;i<length;i++){byte value=window[(start+i)&8191];output[at++]=value;window[pos]=value;pos=(pos+1)&8191;}}
   }
   if(at!=total)throw new InvalidDataException("CMP output length mismatch");
   var result=new byte[count][];at=0;for(int i=0;i<count;i++){result[i]=new byte[sizes[i]];Array.Copy(output,at,result[i],0,sizes[i]);at+=sizes[i];}return result;
  }
  public static NativePixels Tim(byte[] b){
   if(U32(b,0)!=16)throw new InvalidDataException("Invalid TIM signature");int flags=U32(b,4),mode=flags&7,at=8;ushort[] palette=null;
   if(mode>2)throw new InvalidDataException("Unsupported native TIM format");
   if((flags&8)!=0){int size=U32(b,at);if(size<12 || size>b.Length-at || (size&1)!=0)throw new InvalidDataException("Invalid TIM palette extent");int colours=mode==0?16:256;if(size<12+colours*2)throw new InvalidDataException("Truncated TIM palette");palette=new ushort[colours];for(int i=0;i<colours;i++)palette[i]=(ushort)U16(b,at+12+i*2);at+=size;}
   int block=U32(b,at),words=U16(b,at+8),h=U16(b,at+10),w=words*(mode==0?4:mode==1?2:1);
   if(w<1 || h<1 || w>4096 || h>4096 || block<12 || block>b.Length-at || (long)words*h*2>block-12)throw new InvalidDataException("Invalid TIM image extent");
   if(mode<2 && palette==null)throw new InvalidDataException("Missing TIM palette");var result=new NativePixels{Width=w,Height=h,Pixels=new ushort[w*h]};
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){int row=at+12+y*words*2;ushort c=mode==2?(ushort)U16(b,row+x*2):palette[mode==0?(b[row+x/2]>>((x&1)*4))&15:b[row+x]];result.Pixels[y*w+x]=c;}
   return result;
  }
  public byte[] Read(string path,string wad,string entry,bool playstation){
   string key=(playstation?"ps1/":"pc/")+path+"|"+wad+"|"+entry;byte[] bytes;if(files.TryGetValue(key,out bytes))return bytes;
   if(!String.IsNullOrEmpty(wad)){
    Dictionary<string,byte[]> archive;if(!wads.TryGetValue(wad,out archive)){archive=Wad(File.ReadAllBytes(Child(game,wad)));wads.Add(wad,archive);}
    if(!archive.TryGetValue(entry,out bytes))throw new InvalidDataException("Missing WAD texture entry: "+entry);
   }else bytes=File.ReadAllBytes(Child(playstation?ps1:Child(game,"textures/sd"),path));
   files.Add(key,bytes);return bytes;
  }
  public NativePixels Frame(string path,string wad,string entry,bool playstation,int index){
   byte[] bytes=Read(path,wad,entry,playstation);string key=(playstation?"ps1/":"pc/")+path+"|"+wad+"|"+entry;byte[][] frames;
   if(!sequences.TryGetValue(key,out frames)){string name=String.IsNullOrEmpty(wad)?path:entry;frames=name.EndsWith(".cmp",StringComparison.OrdinalIgnoreCase)?Cmp(bytes):new byte[][]{bytes};sequences.Add(key,frames);}
   if(index<0 || index>=frames.Length || frames[index].Length==0)throw new InvalidDataException("Missing native texture frame");return Tim(frames[index]);
  }
  public NativePixels Road(string wad,string library,string table,int material,int level){
   if(level!=128 && level!=64 && level!=32)throw new InvalidDataException("Invalid road mip size");
   byte[] refs=Read(null,wad,table,false);if(material<0 || material>refs.Length/42-1 || refs.Length%42!=0)throw new InvalidDataException("Invalid road layout entry");
   int side=level/32,start=material*42+(level==128?0:level==64?32:40);var result=new NativePixels{Width=level,Height=level,Pixels=new ushort[level*level]};
   for(int i=0;i<side*side;i++){int tile=refs[start+i*2]<<8|refs[start+i*2+1];NativePixels pixels=Frame(null,wad,library,false,tile);if(pixels.Width!=32 || pixels.Height!=32)throw new InvalidDataException("Invalid road tile size");for(int y=0;y<32;y++)Array.Copy(pixels.Pixels,y*32,result.Pixels,(i/side*32+y)*level+i%side*32,32);}
   return result;
  }
  public static void PatchTga(byte[] data,NativePixels image,int x,int y,int width,int height){
   if(data.Length<18 || data[2]!=2 || data[16]!=16)throw new InvalidDataException("Expected native RGB555 TGA");int w=U16(data,12),h=U16(data,14),offset=18+data[0];
   if(width!=image.Width || height!=image.Height || x<0 || y<0 || width>w-x || height>h-y || (long)w*h*2>data.Length-offset)throw new InvalidDataException("Native texture placement is out of bounds");
   for(int row=0;row<height;row++)for(int col=0;col<width;col++){
    ushort ps=image.Pixels[row*width+col];int c=((ps&31)<<10)|(ps&0x83e0)|((ps>>10)&31);int at=offset+((y+row)*w+x+col)*2;data[at]=(byte)c;data[at+1]=(byte)(c>>8);
   }
  }
 }
}
