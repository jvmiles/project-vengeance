// SPDX-License-Identifier: GPL-2.0-only
// Second black-box probe of CDraw behaviour through its public API (Stage 2A evidence).
// Each section prints what GDrawCairo relies on; see docs/GDRAW_CAIRO_STAGE2A.md.
// Build: g++ -std=c++17 -w -I tests/native -I GDraw tests/native/gdraw_conventions_probe2.cpp \
//        -L libs/x86_64 -lCDraw -no-pie -o /tmp/probe2
#include "gdraw_reference.h"
#include <cstdio>
#include <vector>

static const INT32 U = kRefUnitsPerPixel;

static BYTE* Alloc(UINT32 Size) { return static_cast<BYTE*>(std::malloc(Size)); }
static void Release(BYTE* p) { std::free(p); }

static GCONTEXT* NewContext(std::vector<DWORD>& Px, INT32 W, INT32 H, DWORD Compression, bool AA)
{
	GCONTEXT* c = static_cast<GCONTEXT*>(std::calloc(1, GDraw_ContextLength()));
	GDraw_Initialise(c, NULL);
	GDraw_SetMemoryHandlers(c, Alloc, Release);	// strokes need CDraw's allocator
	static BITMAPINFOHEADER h;
	std::memset(&h, 0, sizeof h);
	h.biSize = sizeof h; h.biWidth = W; h.biHeight = H; h.biPlanes = 1; h.biBitCount = 32; h.biCompression = Compression;
	GDraw_SetDIBitmap(c, &h, reinterpret_cast<BYTE*>(Px.data()), 2);
	GMATRIX m = RefPixelMatrix();
	GDraw_SetMatrix(c, &m);
	GDraw_SetAntialiasFlag(c, AA);
	return c;
}
static void Free(GCONTEXT* c) { GDraw_Terminate(c); std::free(c); }
static void Rect(GCONTEXT* c, double x0, double y0, double x1, double y1)
{
	RECT r = { INT32(x0 * U), INT32(y1 * U), INT32(x1 * U), INT32(y0 * U) };
	GDraw_FillRectangle(c, &r);
}
static void Box(GCONTEXT* c, double x0, double y0, double x1, double y1)
{
	POINT q[4] = { {INT32(x0 * U), INT32(y0 * U)}, {INT32(x1 * U), INT32(y0 * U)}, {INT32(x1 * U), INT32(y1 * U)}, {INT32(x0 * U), INT32(y1 * U)} };
	BYTE v[4] = { PT_MOVETO, PT_LINETO, PT_LINETO, BYTE(PT_LINETO | PT_CLOSEFIGURE) };
	GDraw_FillPath(c, q, v, 4, 0);
}

static void Hinting()
{
	std::printf("1. Hinting (AA, 1px stroke, x from 4.24 to 11.6): covered row and end coverage\n");
	for (double y : { 4.0, 4.49, 4.5, 4.99 })
	{
		std::vector<DWORD> px(16 * 12, 0x00ffffff);
		GCONTEXT* c = NewContext(px, 16, 12, BI_RGB, true);
		GDraw_SetHintingFlag(c, true);
		GColour_SetColour(c, 0);
		POINT p[2] = { {INT32(4.24 * U), INT32(y * U)}, {INT32(11.6 * U), INT32(y * U)} };
		BYTE v[2] = { PT_MOVETO, PT_LINETO };
		GDraw_StrokePath(c, p, v, 2, false, DWORD(U), CAPS_BUTT, JOIN_MITER, NULL);
		for (int r = 0; r < 12; r++)
			if ((px[r * 16 + 8] & 0xff) == 0)
				std::printf("   y=%.2f -> device row %d; coverage at x=4: %d, x=11: %d\n", y, r,
							255 - int(px[r * 16 + 4] & 0xff), 255 - int(px[r * 16 + 11] & 0xff));
		Free(c);
	}
	std::printf("   diagonal strokes are unchanged by hinting (see gdraw_conventions_probe.cpp Q8 and docs)\n");
}

static void PaintReset()
{
	std::printf("2. A colour call replaces gradient transparency and bitmap fills\n");
	std::vector<DWORD> px(16 * 4, 0x00ffffff);
	GCONTEXT* c = NewContext(px, 16, 4, BI_RGB, false);
	TransparentGradTable t;
	for (int i = 0; i < 256; i++) t.Table[i] = BYTE(i);
	POINT a = { 0, 0 }, b = { 16 * U, 0 }, cc = { 0, 16 * U };
	GColour_SetColour(c, 0);
	GColour_SetTransparentGraduation(c, 0, &t, &a, &b, &cc);
	GColour_SetTransparency(c, 0, T_GRAD_REFLECTIVE);
	Rect(c, 0, 0, 1, 1);
	GColour_SetColour(c, 0);
	Rect(c, 1, 0, 2, 1);
	DWORD tile[4] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00 };
	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof bi; bi.biWidth = 2; bi.biHeight = 2; bi.biPlanes = 1; bi.biBitCount = 32;
	POINT bp[3] = { {0, 0}, {2 * U, 0}, {0, 2 * U} };
	GColour_SetTilePattern(c, &bi, reinterpret_cast<BYTE*>(tile), 0, &bp[0], &bp[1], &bp[2]);
	Rect(c, 2, 0, 3, 1);
	GColour_SetColour(c, 0);
	Rect(c, 3, 0, 4, 1);
	std::printf("   grad-transparent %06x, then SetColour %06x; tile %06x, then SetColour %06x\n",
				px[0] & 0xffffff, px[1] & 0xffffff, px[2] & 0xffffff, px[3] & 0xffffff);
	Free(c);
}

static void Rebind()
{
	std::printf("3. SetDIBitmap resets the changed box and the device clip\n");
	std::vector<DWORD> px(64, 0x00ffffff), other(64, 0x00ffffff);
	GCONTEXT* c = NewContext(px, 8, 8, BI_RGB, false);
	GColour_SetColour(c, 0);
	GDraw_ClearChangedBBox(c);
	Rect(c, 0, 0, 2, 2);
	RECT dc = { 1, 1, 5, 5 };
	GDraw_DeviceClipRectangle(c, &dc);
	BITMAPINFOHEADER h = {};
	h.biSize = sizeof h; h.biWidth = 8; h.biHeight = 8; h.biPlanes = 1; h.biBitCount = 32;
	GDraw_SetDIBitmap(c, &h, reinterpret_cast<BYTE*>(other.data()), 2);
	RECT bb, cl;
	GDraw_GetChangedBBox(c, &bb);
	GDraw_GetDeviceClipRectangle(c, &cl);
	std::printf("   changed %d %d %d %d (empty), clip %d %d %d %d (full)\n", bb.left, bb.top, bb.right, bb.bottom, cl.left, cl.top, cl.right, cl.bottom);
	Free(c);
}

static void TopByte()
{
	std::printf("4. Top byte over a 0x40 background: rect | path full | path partial | stroke | point\n");
	const char* Names[] = { "SetColour", "SetSolidColour 32", "T_NONE", "T_REFLECTIVE" };
	for (int aa = 0; aa <= 1; aa++)
		for (int k = 0; k < 4; k++)
		{
			std::vector<DWORD> px(12 * 2, 0x40ffffff);
			GCONTEXT* c = NewContext(px, 12, 2, BI_RGB, aa);
			const COLORREF col = RGB(0x11, 0x22, 0x33);
			if (k == 0) GColour_SetColour(c, col);
			if (k == 1) GColour_SetSolidColour(c, col, 32, 2);
			if (k == 2) GColour_SetTransparency(c, col, T_NONE);
			if (k == 3) GColour_SetTransparency(c, col, T_REFLECTIVE);
			Rect(c, 0, 0, 1, 1);
			Box(c, 2, 0, 3.5, 1);
			POINT s[2] = { {6 * U, 0}, {6 * U, 1 * U} };
			BYTE sv[2] = { PT_MOVETO, PT_LINETO };
			GDraw_StrokePath(c, s, sv, 2, false, DWORD(2 * U), CAPS_BUTT, JOIN_MITER, NULL);
			POINT q = { INT32(8.2 * U), INT32(0.2 * U) };
			GDraw_FillPoint(c, &q);
			std::printf("   aa=%d %-18s %08x | %08x | %08x | %08x | %08x\n", aa, Names[k], px[0], px[2], px[3], px[5], px[8]);
			Free(c);
		}
}

static void FourPixelGroups()
{
	std::printf("5. AA solid spans write whole words only for groups [4k,4k+4) inside a full run\n");
	std::printf("   (0 = word written, K = top byte kept, . = untouched; groups follow x, not addresses)\n");
	for (int W : { 23, 24 })
	{
		std::vector<DWORD> px(W, 0x40ffffff);
		GCONTEXT* c = NewContext(px, W, 1, BI_RGB, true);
		GColour_SetColour(c, 0);
		Box(c, 1, 0, W - 1, 1);
		std::printf("   W=%d x[1,%d): ", W, W - 1);
		for (int x = 0; x < W; x++)
			std::putchar((px[x] & 0xffffff) == 0xffffff ? '.' : ((px[x] >> 24) ? 'K' : '0'));
		std::printf("\n");
		Free(c);
	}
}

static void RGBT()
{
	std::printf("6. Opaque fills into 0x80000001 (RGBT, T = 255 - alpha) bitmaps (Stage 2B input)\n");
	for (DWORD bg : { 0xff000000u, 0x80404040u })
	{
		std::vector<DWORD> px(16, bg);
		GCONTEXT* c = NewContext(px, 16, 1, 0x80000001, true);
		GColour_SetColour(c, RGB(0x20, 0x40, 0xc0));
		Rect(c, 0, 0, 2, 1);
		Box(c, 4, 0, 9.5, 1);
		std::printf("   bg %08x: rect %08x, full %08x, half-covered %08x\n", bg, px[0], px[4], px[9]);
		Free(c);
	}
}

int main()
{
	std::printf("CDraw %u.%u\n", GDraw_GetVersion() >> 16, GDraw_GetVersion() & 0xffff);
	Hinting();
	PaintReset();
	Rebind();
	TopByte();
	FourPixelGroups();
	RGBT();
	return 0;
}
