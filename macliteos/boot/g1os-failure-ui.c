/* Independent framebuffer diagnostic screen. It is intentionally separate from
 * mica-comp so early graphics/compositor failures remain visible. */
#include <linux/fb.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void glyph(char c, uint8_t out[7])
{
    static const uint8_t space[7]={0,0,0,0,0,0,0};
    static const uint8_t q[7]={0x0E,0x11,0x01,0x02,0x04,0,0x04};
    const uint8_t *g=space;
    static const uint8_t A[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},B[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},C[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},D[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},E[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},F[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},G[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},H[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11},I[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},J[7]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C},K[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11},L[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F},M[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11},N[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11},O[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},P[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},Q[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},R[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},S[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},T[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04},U[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E},V[7]={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},W[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11},X[7]={0x11,0x0A,0x0A,0x04,0x0A,0x0A,0x11},Y[7]={0x11,0x0A,0x0A,0x04,0x04,0x04,0x04},Z[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
    switch(c){case'A':g=A;break;case'B':g=B;break;case'C':g=C;break;case'D':g=D;break;case'E':g=E;break;case'F':g=F;break;case'G':g=G;break;case'H':g=H;break;case'I':g=I;break;case'J':g=J;break;case'K':g=K;break;case'L':g=L;break;case'M':g=M;break;case'N':g=N;break;case'O':g=O;break;case'P':g=P;break;case'Q':g=Q;break;case'R':g=R;break;case'S':g=S;break;case'T':g=T;break;case'U':g=U;break;case'V':g=V;break;case'W':g=W;break;case'X':g=X;break;case'Y':g=Y;break;case'Z':g=Z;break;case'?':g=q;break;case'-':{static const uint8_t x[7]={0,0,0,0x1F,0,0,0};g=x;break;}case':':{static const uint8_t x[7]={0,0x04,0,0,0,0x04,0};g=x;break;}case'.':{static const uint8_t x[7]={0,0,0,0,0,0x06,0x06};g=x;break;}case'/':{static const uint8_t x[7]={1,2,2,4,8,8,16};g=x;break;}default:break;}
    memcpy(out,g,7);
}

static void px(uint8_t *fb,long stride,int bpp,int x,int y,uint32_t c){if(bpp==32)*(uint32_t*)(fb+y*stride+x*4)=c;else *(uint16_t*)(fb+y*stride+x*2)=(uint16_t)(((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31));}
static void rect(uint8_t *fb,long stride,int bpp,int w,int h,int x,int y,int rw,int rh,uint32_t c){for(int yy=0;yy<rh;yy++)for(int xx=0;xx<rw;xx++)if(x+xx>=0&&y+yy>=0&&x+xx<w&&y+yy<h)px(fb,stride,bpp,x+xx,y+yy,c);}
static void text(uint8_t *fb,long stride,int bpp,int w,int h,int x,int y,const char *s,int scale,uint32_t c){for(;*s;s++,x+=6*scale){uint8_t g[7];char ch=*s;if(ch>='a'&&ch<='z')ch=(char)(ch-'a'+'A');glyph(ch,g);for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(g[yy]&(1<<(4-xx)))rect(fb,stride,bpp,w,h,x+xx*scale,y+yy*scale,scale,scale,c);}}

int main(int argc,char **argv)
{
    const char *msg=argc>2&&strcmp(argv[1],"--message")==0?argv[2]:"GRAPHICAL STARTUP FAILED";
    int fd=open("/dev/fb0",O_RDWR); if(fd<0)return 2;
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v)<0||ioctl(fd,FBIOGET_FSCREENINFO,&f)<0){close(fd);return 3;}
    if(v.bits_per_pixel!=16&&v.bits_per_pixel!=32){close(fd);return 4;}
    size_t len=(size_t)f.line_length*v.yres; uint8_t *fb=mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(fb==MAP_FAILED){close(fd);return 5;}
    rect(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,0,0,v.xres,v.yres,0x00182028);
    rect(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,0,0,v.xres,8,0x003b82c4);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,55,"G1OS INSTALLER",3,0x00ffffff);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,105,"GRAPHICAL STARTUP FAILED",2,0x00ffcc66);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,155,msg,2,0x00ffffff);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,215,"SEE BOOT LOG AND COMPOSITOR LOG",1,0x00b9c6d3);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,235,"/RUN/G1OS-BOOT.LOG",1,0x00b9c6d3);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,255,"/RUN/G1OS/MICA-COMP.LOG",1,0x00b9c6d3);
    text(fb,f.line_length,v.bits_per_pixel,v.xres,v.yres,48,275,"/RUN/G1OS/MICA-INSTALLER.LOG",1,0x00b9c6d3);
    msync(fb,len,MS_SYNC); munmap(fb,len); close(fd); return 0;
}
