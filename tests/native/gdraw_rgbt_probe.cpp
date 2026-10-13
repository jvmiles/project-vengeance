// SPDX-License-Identifier: GPL-2.0-only
// Black-box probe of CDraw writing opaque colours into transparency-channel (0x80000001)
// bitmaps, through its public API (Stage 2B evidence; see docs/GDRAW_CAIRO_STAGE2B.md).
// R1: every draw operation x colour call x AA mode over a 50%-transparent background
//     (the "stroke" cell is a slightly diagonal 1px line, i.e. a partial pixel).
// R2: partial-coverage arithmetic over six backgrounds, with the coverage measured from
//     the same geometry drawn black on white into a BI_RGB bitmap.
// Build: g++ -std=c++17 -w -I tests/native -I GDraw tests/native/gdraw_rgbt_probe.cpp
//        -L libs/x86_64 -lCDraw -no-pie -o /tmp/rgbt_probe
#include "gdraw_reference.h"
#include <cstdio>
#include <vector>
static const INT32 U=1024;
static BYTE* A(UINT32 s){return (BYTE*)malloc(s);} static void F(BYTE*p){free(p);}
static GCONTEXT* Ctx(std::vector<DWORD>& px,int W,int H,DWORD comp,bool aa){ GCONTEXT* c=(GCONTEXT*)calloc(1,GDraw_ContextLength()); GDraw_Initialise(c,NULL); GDraw_SetMemoryHandlers(c,A,F);
 static BITMAPINFOHEADER h; memset(&h,0,sizeof h); h.biSize=sizeof h; h.biWidth=W; h.biHeight=H; h.biPlanes=1; h.biBitCount=32; h.biCompression=comp;
 GDraw_SetDIBitmap(c,&h,(BYTE*)px.data(),2); GMATRIX m=RefPixelMatrix(); GDraw_SetMatrix(c,&m); GDraw_SetAntialiasFlag(c,aa); return c; }
static void Box(GCONTEXT* c,double x0,double y0,double x1,double y1){ POINT q[4]={{INT32(x0*U),INT32(y0*U)},{INT32(x1*U),INT32(y0*U)},{INT32(x1*U),INT32(y1*U)},{INT32(x0*U),INT32(y1*U)}}; BYTE v[4]={PT_MOVETO,PT_LINETO,PT_LINETO,BYTE(PT_LINETO|PT_CLOSEFIGURE)}; GDraw_FillPath(c,q,v,4,0);}
int main(){
  const char* names[]={"SetColour","Solid32","T_NONE","T_REFL"};
  printf("R1/R3: RGBT bg 80404040, colour 2040c0. cells: rect | path run x[1,22) at x=1,2,3,4,20,21 | partial x=22 (x[1,22.5)) | stroke | point\n");
  for(int aa=0;aa<=1;aa++) for(int k=0;k<4;k++){
    std::vector<DWORD> px(32*3,0x80404040); GCONTEXT* c=Ctx(px,32,3,0x80000001,aa); COLORREF col=RGB(0x20,0x40,0xc0);
    if(k==0)GColour_SetColour(c,col); if(k==1)GColour_SetSolidColour(c,col,32,2); if(k==2)GColour_SetTransparency(c,col,T_NONE); if(k==3)GColour_SetTransparency(c,col,T_REFLECTIVE);
    RECT r={30*U,1*U,31*U,0}; GDraw_FillRectangle(c,&r);
    Box(c,1,1,22.5,2);   // row 1
    POINT s[2]={{24*U,2*U},{29*U,2*U+U/2}}; BYTE sv[2]={PT_MOVETO,PT_LINETO}; // row 2 stroke  horizontal y=2.5 w=1 -> row 2 full
    GDraw_StrokePath(c,s,sv,2,false,DWORD(U),CAPS_BUTT,JOIN_MITER,NULL);
    POINT q={INT32(28.2*U),INT32(0.2*U)}; GDraw_FillPoint(c,&q);
    int W=32; auto P=[&](int x,int y){return px[y*W+x];};
    printf(" aa=%d %-8s rect %08x | %08x %08x %08x %08x .. %08x %08x | part %08x | stroke %08x | point %08x\n",aa,names[k],P(30,0),P(1,1),P(2,1),P(3,1),P(4,1),P(20,1),P(21,1),P(22,1),P(26,2),P(28,0));
    GDraw_Terminate(c); free(c);
  }
  printf("R2: partial coverage arithmetic: half-covered pixel (x[0,0.5)) and quarter (y) over bgs\n");
  for(DWORD bg: {0xff000000u,0x00ffffffu,0x80404040u,0xc0102030u,0x00000000u,0x40806040u}) {
    std::vector<DWORD> px(4*4,bg), cov(4*4,0x00ffffff); GCONTEXT* c=Ctx(px,4,4,0x80000001,true); GCONTEXT* cc=Ctx(cov,4,4,BI_RGB,true);
    GColour_SetColour(c,RGB(0x20,0x40,0xc0)); GColour_SetColour(cc,0);
    for(GCONTEXT* g: {c,cc}){ POINT q[4]={{0,0},{U/2,0},{U/2,U},{0,U}}; BYTE v[4]={PT_MOVETO,PT_LINETO,PT_LINETO,BYTE(PT_LINETO|PT_CLOSEFIGURE)}; GDraw_FillPath(g,q,v,4,0);
      POINT q2[4]={{2*U,2*U},{3*U,2*U},{3*U,2*U+U/4},{2*U,2*U+U/4}}; GDraw_FillPath(g,q2,v,4,0);}
    int c1=255-(cov[0]&0xff), c2=255-(cov[2*4+2]&0xff);
    printf("  bg %08x: half cov=%3d -> %08x   quarter cov=%3d -> %08x\n",bg,c1,px[0],c2,px[2*4+2]);
    GDraw_Terminate(c); free(c); GDraw_Terminate(cc); free(cc);
  }
}
