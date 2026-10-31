/* MIT. Bounded spectral noise reduction and active-passage loudness matching.
 * Works on user-supplied native22050Hz mono PCM16 WAVs; retains every RIFF
 * byte outside the PCM payload, its sample count, and the original clock. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#define N 512
#define H 128
#define PI 3.14159265358979323846
typedef struct {double r,i;} Complex;
static void fft(Complex *x,int inverse) {
    for(int i=1,j=0;i<N;i++) {int bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){Complex t=x[i];x[i]=x[j];x[j]=t;}}
    for(int len=2;len<=N;len<<=1) {
        double angle=(inverse?2:-2)*PI/len,wr0=cos(angle),wi0=sin(angle);
        for(int base=0;base<N;base+=len) {double wr=1,wi=0;
            for(int j=0;j<len/2;j++) {Complex a=x[base+j],b=x[base+j+len/2];double r=b.r*wr-b.i*wi,i=b.r*wi+b.i*wr;
                x[base+j]=(Complex){a.r+r,a.i+i};x[base+j+len/2]=(Complex){a.r-r,a.i-i};double next=wr*wr0-wi*wi0;wi=wr*wi0+wi*wr0;wr=next;
            }
        }
    }if(inverse)for(int i=0;i<N;i++){x[i].r/=N;x[i].i/=N;}
}
static int compare(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static int named(const wchar_t *s,const wchar_t *list) {
    const wchar_t *p=s;for(const wchar_t *q=s;*q;q++)if(*q==L'\\'||*q==L'/')p=q+1;
    wchar_t name[80];int n=0;while(p[n] && p[n]!=L'.' && n<77){name[n]=towupper(p[n]);n++;}name[n++]=L'|';name[n]=0;
    return wcsstr(list,name)!=NULL;
}
static double shape(double x,double gain){x*=gain;double a=fabs(x);return copysign(a<=32000?a:32000+766*tanh((a-32000)/766),x);}
static uint32_t u32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const unsigned char *p){return p[0]|(uint16_t)p[1]<<8;}
int wmain(int argc,wchar_t **argv) {
    if(argc!=3){fwprintf(stderr,L"Usage: audio-clean.exe original.wav cleaned.wav\n");return 2;}
    FILE *f=_wfopen(argv[1],L"rb");if(!f)return 3;fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);
    if(length<44 || length>32*1024*1024){fclose(f);return 4;}unsigned char *blob=malloc(length);if(!blob){fclose(f);return 5;}
    if(fread(blob,1,length,f)!=(size_t)length){fclose(f);free(blob);return 6;}fclose(f);
    if(memcmp(blob,"RIFF",4)||memcmp(blob+8,"WAVE",4)){free(blob);return 7;}
    size_t data=0,bytes=0;int format=0;
    for(size_t at=12;at+8<=(size_t)length;) {uint32_t n=u32(blob+at+4);if(n>(size_t)length-at-8){free(blob);return 8;}
        if(!memcmp(blob+at,"fmt ",4)&&n>=16)format=u16(blob+at+8)==1&&u16(blob+at+10)==1&&u32(blob+at+12)==22050&&u16(blob+at+22)==16;
        if(!memcmp(blob+at,"data",4)){data=at+8;bytes=n;}at+=8+n+(n&1);
    }if(!format||!data||bytes<2||(bytes&1)){free(blob);return 9;}
    int count=(int)(bytes/2),frames=(count+N+H-1)/H;
    double *dry=calloc(count,sizeof(double)),*wet=calloc(count,sizeof(double)),*weight=calloc(count,sizeof(double)),*powers=malloc(frames*sizeof(double));
    Complex *spectra=calloc((size_t)frames*N,sizeof(Complex));
    if(!dry||!wet||!weight||!powers||!spectra){free(blob);free(dry);free(wet);free(weight);free(powers);free(spectra);return 10;}
    double window[N],noise[N/2+1],smooth[N/2+1];
    for(int i=0;i<N;i++)window[i]=sqrt(0.5-0.5*cos(2*PI*(i+0.5)/N));
    for(int i=0;i<count;i++)dry[i]=(int16_t)u16(blob+data+i*2);
    for(int frame=0;frame<frames;frame++) {
        int start=frame*H-N;Complex *s=spectra+(size_t)frame*N;
        for(int i=0;i<N;i++){int sample=start+i;if(sample>=0&&sample<count)s[i].r=dry[sample]*window[i];}fft(s,0);
    }
    for(int bin=0;bin<=N/2;bin++) {
        int used=0;for(int frame=0;frame<frames;frame++){Complex a=spectra[(size_t)frame*N+bin];double p=a.r*a.r+a.i*a.i;if(p>0.001)powers[used++]=p;}
        if(used){qsort(powers,used,sizeof(double),compare);noise[bin]=powers[(used-1)/8]*0.5;}else noise[bin]=0;smooth[bin]=1;
    }
    double reduction=12,mix=1;
    if(named(argv[1],L"CROWD|DROID|ENGINES|SCRAPE|TURBLNCE|")){reduction=5;mix=.55;}
    else if(named(argv[1],L"HITWALL2|SHP2SHP|SHPSTALL|THUNDER|WEAPNHIT|WEPNWALL|")){reduction=7;mix=.8;}
    else if(named(argv[1],L"MENUMOVE|MENUSEL|MENUSLID|PICKUP|RECHARGE|")){reduction=5;mix=.7;}
    double floor_gain=pow(10,-reduction/20);
    for(int frame=0;frame<frames;frame++) {
        Complex *s=spectra+(size_t)frame*N;
        for(int bin=0;bin<=N/2;bin++) {
            double p=s[bin].r*s[bin].r+s[bin].i*s[bin].i,average=0;int terms=0;
            for(int j=bin-2;j<=bin+2;j++)if(j>=0&&j<=N/2){average+=noise[j];terms++;}
            average/=terms;
            double gain=sqrt(fmax(0,1-average/fmax(p,0.000001)));gain=fmax(floor_gain,gain);
            double memory=gain<smooth[bin]?.65:.2;smooth[bin]=smooth[bin]*memory+gain*(1-memory);gain=smooth[bin];
            s[bin].r*=gain;s[bin].i*=gain;if(bin>0&&bin<N/2){s[N-bin].r*=gain;s[N-bin].i*=gain;}
        }fft(s,1);int start=frame*H-N;
        for(int i=0;i<N;i++){int sample=start+i;if(sample>=0&&sample<count){wet[sample]+=s[i].r*window[i];weight[sample]+=window[i]*window[i];}}
    }
    for(int i=0;i<count;i++){double cleaned=weight[i]>1e-12?wet[i]/weight[i]:dry[i];wet[i]=dry[i]+mix*(cleaned-dry[i]);}
    int blocks=count/256;double maximum=0,target=0;
    for(int block=0;block<blocks;block++){double e=0;for(int j=0;j<256;j++)e+=dry[block*256+j]*dry[block*256+j];powers[block]=e;if(e>maximum)maximum=e;}
    for(int block=0;block<blocks;block++)if(powers[block]>=maximum*.1)target+=powers[block];
    double lo=.5,hi=2,gain=1;
    if(target>0)for(int iteration=0;iteration<45;iteration++){
        gain=(lo+hi)*.5;double energy=0;for(int block=0;block<blocks;block++)if(powers[block]>=maximum*.1)for(int j=0;j<256;j++){double v=shape(wet[block*256+j],gain);energy+=v*v;}
        if(energy<target)lo=gain;else hi=gain;
    }
    for(int i=0;i<count;i++){long v=lround(shape(wet[i],gain));if(v>32766)v=32766;if(v< -32766)v=-32766;uint16_t sample=(uint16_t)(int16_t)v;blob[data+i*2]=(unsigned char)sample;blob[data+i*2+1]=(unsigned char)(sample>>8);}
    f=_wfopen(argv[2],L"wb");int result=0;if(!f)result=11;else{if(fwrite(blob,1,length,f)!=(size_t)length)result=12;fclose(f);}
    free(blob);free(dry);free(wet);free(weight);free(powers);free(spectra);return result;
}
