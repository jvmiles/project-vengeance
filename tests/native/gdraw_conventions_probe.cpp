// SPDX-License-Identifier: GPL-2.0-only
// Black-box probe of CDraw rendering conventions through its public API only.
// Output documents the behaviour GDrawCairo must reproduce; it is not a pass/fail test.
#include "gdraw_reference.h"
#include <cstdio>

static const INT32 U = kRefUnitsPerPixel;
static const INT32 W = 12, H = 8;

static void DumpRow(RefGDraw& g, INT32 MemRow, const char* pLabel)
{
	std::printf("  %-28s", pLabel);
	for (INT32 x = 0; x < W; x++) std::printf(" %08x", g.Pixel(x, MemRow));
	std::printf("\n");
}

static void Rect(RefGDraw& g, double x0, double y0, double x1, double y1)
{
	RECT r = { INT32(x0 * U), INT32(y1 * U), INT32(x1 * U), INT32(y0 * U) };	// top = high y
	GDraw_FillRectangle(g.Context(), &r);
}

static RefGDraw* Fresh(bool AA, bool Inverted = true)
{
	RefGDraw* g = new RefGDraw(W, H, Inverted, 0x00ffffff);
	GMATRIX m = RefPixelMatrix();
	GDraw_SetMatrix(g->Context(), &m);
	GDraw_SetAntialiasFlag(g->Context(), AA);
	GColour_SetColour(g->Context(), RGB(0x11, 0x22, 0x33));
	return g;
}

static void PrintBBox(RefGDraw& g, const char* pLabel)
{
	RECT b;
	GDraw_GetChangedBBox(g.Context(), &b);
	std::printf("  %-28s left=%d top=%d right=%d bottom=%d\n", pLabel, b.left, b.top, b.right, b.bottom);
}

int main()
{
	std::printf("CDraw %u.%u context %d bytes\n", GDraw_GetVersion() >> 16, GDraw_GetVersion() & 0xffff, GDraw_ContextLength());

	std::printf("Q1/Q2 pixel format and orientation: non-AA rect x[1,3) y[0,1) px, white background\n");
	for (int inv = 1; inv >= 0; inv--)
	{
		RefGDraw* g = Fresh(false, inv);
		std::printf(" %s DIB (set=%d)\n", inv ? "inverted" : "normal", g->SetResult());
		Rect(*g, 1, 0, 3, 1);
		DumpRow(*g, 0, "memory row 0");
		DumpRow(*g, H - 1, "memory row H-1");
		PrintBBox(*g, "changed bbox");
		delete g;
	}

	std::printf("Q3 non-AA sampling (memory row H-1 = device row 0 when inverted)\n");
	const double Cases[][2] = { {2.0, 5.0}, {2.5, 5.5}, {2.49, 5.49}, {2.51, 5.51}, {2.25, 5.75}, {2.75, 5.25} };
	for (const auto& c : Cases)
	{
		RefGDraw* g = Fresh(false);
		Rect(*g, c[0], 0, c[1], 1);
		char label[64]; std::snprintf(label, sizeof label, "x [%.2f,%.2f)", c[0], c[1]);
		DumpRow(*g, H - 1, label);
		delete g;
	}
	std::printf("Q3b non-AA vertical: y [0.5,2.5) at x[1,3): rows bottom-up\n");
	{
		RefGDraw* g = Fresh(false);
		Rect(*g, 1, 0.5, 3, 2.5);
		for (INT32 r = H - 1; r >= H - 4; r--) { char l[32]; std::snprintf(l, sizeof l, "device row %d", H - 1 - r); DumpRow(*g, r, l); }
		delete g;
	}

	std::printf("Q4 AA coverage\n");
	const double AACases[][2] = { {2.0, 5.0}, {2.25, 5.75}, {2.5, 5.5}, {2.125, 5.875} };
	for (const auto& c : AACases)
	{
		RefGDraw* g = Fresh(true);
		Rect(*g, c[0], 0, c[1], 1);
		char label[64]; std::snprintf(label, sizeof label, "AA x [%.3f,%.3f)", c[0], c[1]);
		DumpRow(*g, H - 1, label);
		delete g;
	}
	for (int q = 0; q <= 1; q++)
	{
		RefGDraw* g = Fresh(true);
		GDraw_SetAntialiasQualityFlag(g->Context(), q);
		Rect(*g, 2.3, 0, 5.7, 1);
		char label[64]; std::snprintf(label, sizeof label, "AA quality=%d x[2.3,5.7)", q);
		DumpRow(*g, H - 1, label);
		delete g;
	}

	std::printf("Q5 FillRectangle goes through the matrix (origin +2px)\n");
	{
		RefGDraw* g = Fresh(false);
		GMATRIX m = RefPixelMatrix(2 * U, 0);
		GDraw_SetMatrix(g->Context(), &m);
		Rect(*g, 1, 0, 3, 1);
		DumpRow(*g, H - 1, "x [1,3) + origin 2");
		delete g;
	}

	std::printf("Q6 changed bbox for AA rect x[2.25,5.75) y[1.5,3.5)\n");
	{
		RefGDraw* g = Fresh(true);
		GDraw_ClearChangedBBox(g->Context());
		PrintBBox(*g, "after clear");
		Rect(*g, 2.25, 1.5, 5.75, 3.5);
		PrintBBox(*g, "after fill");
		delete g;
	}

	std::printf("Q7 clipping: ClipRectangle doc x[0,4) then fill x[2,8)\n");
	{
		RefGDraw* g = Fresh(false);
		RECT c = { 0, 8 * U, 4 * U, 0 };
		std::printf("  ClipRectangle -> %d\n", GDraw_ClipRectangle(g->Context(), &c));
		RECT dc; GDraw_GetDeviceClipRectangle(g->Context(), &dc);
		std::printf("  device clip after: %d %d %d %d\n", dc.left, dc.top, dc.right, dc.bottom);
		Rect(*g, 2, 0, 8, 1);
		DumpRow(*g, H - 1, "clipped");
		RECT c2 = { 3 * U, 8 * U, 10 * U, 0 };
		GDraw_ClipRectangle(g->Context(), &c2);
		Rect(*g, 0, 1, 12, 2);
		DumpRow(*g, H - 2, "second clip x[3,10) replaces?");
		delete g;
	}
	{
		RefGDraw* g = Fresh(false);
		RECT dc0; GDraw_GetDeviceClipRectangle(g->Context(), &dc0);
		std::printf("  default device clip: %d %d %d %d\n", dc0.left, dc0.top, dc0.right, dc0.bottom);
		RECT d = { 1, 2, 6, 7 };
		std::printf("  DeviceClipRectangle(1,2,6,7) -> %d\n", GDraw_DeviceClipRectangle(g->Context(), &d));
		GDraw_GetDeviceClipRectangle(g->Context(), &dc0);
		std::printf("  device clip now: %d %d %d %d\n", dc0.left, dc0.top, dc0.right, dc0.bottom);
		Rect(*g, 0, 0, 12, 8);
		for (INT32 r = 0; r < H; r++) { char l[32]; std::snprintf(l, sizeof l, "mem row %d", r); DumpRow(*g, r, l); }
		RECT c = { 0, 8 * U, 4 * U, 0 };
		GDraw_ClipRectangle(g->Context(), &c);
		GDraw_GetDeviceClipRectangle(g->Context(), &dc0);
		std::printf("  device clip after ClipRectangle x[0,4): %d %d %d %d\n", dc0.left, dc0.top, dc0.right, dc0.bottom);
		delete g;
	}

	std::printf("Q8 strokes: horizontal line y=4px x 1..10, widths, hinting\n");
	const double Widths[] = { 0, 0.5, 1, 2, 3 };
	for (int hint = 0; hint <= 1; hint++)
		for (double w : Widths)
		{
			RefGDraw* g = Fresh(true);
			GDraw_SetHintingFlag(g->Context(), hint);
			POINT p[2] = { { 1 * U, 4 * U }, { 10 * U, 4 * U } };
			BYTE v[2] = { PT_MOVETO, PT_LINETO };
			INT32 rc = GDraw_StrokePath(g->Context(), p, v, 2, false, DWORD(w * U), CAPS_BUTT, JOIN_MITER, NULL);
			std::printf(" hint=%d width=%.1fpx rc=%d\n", hint, w, rc);
			for (INT32 r = H - 7; r <= H - 3; r++) { char l[32]; std::snprintf(l, sizeof l, "device row %d", H - 1 - r); DumpRow(*g, r, l); }
			delete g;
		}

	std::printf("Q9 colour entry points (32bpp)\n");
	{
		const DWORD c = RGB(0x12, 0x34, 0x56);
		RefGDraw* g = Fresh(false);
		GColour_SetSolidColour(g->Context(), c, 32, 2); Rect(*g, 0, 0, 1, 1);
		GColour_SetSolidColour(g->Context(), c, 24, 2); Rect(*g, 1, 0, 2, 1);
		GColour_SetSolidColour(g->Context(), c, 16, 2); Rect(*g, 2, 0, 3, 1);
		GColour_SetSolidColour(g->Context(), c, 16, 0); Rect(*g, 3, 0, 4, 1);
		GColour_SetSolidColour(g->Context(), c, 8, 0); Rect(*g, 4, 0, 5, 1);
		GColour_SetSolidColour(g->Context(), c, 0, 2); Rect(*g, 5, 0, 6, 1);
		GColour_SetColour(g->Context(), c); Rect(*g, 6, 0, 7, 1);
		GColour_SetTransparency(g->Context(), c, T_NONE); Rect(*g, 7, 0, 8, 1);
		GColour_SetTransparency(g->Context(), c, T_REFLECTIVE); Rect(*g, 8, 0, 9, 1);
		GColour_SetTransparency(g->Context(), c | (0x80u << 24), T_REFLECTIVE); Rect(*g, 9, 0, 10, 1);
		GColour_SetWordColour(g->Context(), 0x7f00ffff); Rect(*g, 10, 0, 11, 1);
		DumpRow(*g, H - 1, "solid32 24 16/2 16/0 8 0 col T0 TR TR80 word");
		delete g;
	}

	std::printf("Q10 winding: two overlapping squares, same direction\n");
	for (DWORD wind = 0; wind <= 3; wind++)
	{
		RefGDraw* g = Fresh(false);
		POINT p[10] = { {1*U,0},{5*U,0},{5*U,4*U},{1*U,4*U},{1*U,0}, {3*U,2*U},{7*U,2*U},{7*U,6*U},{3*U,6*U},{3*U,2*U} };
		BYTE v[10] = { PT_MOVETO, PT_LINETO, PT_LINETO, PT_LINETO, BYTE(PT_LINETO | PT_CLOSEFIGURE),
					   PT_MOVETO, PT_LINETO, PT_LINETO, PT_LINETO, BYTE(PT_LINETO | PT_CLOSEFIGURE) };
		INT32 rc = GDraw_FillPath(g->Context(), p, v, 10, wind);
		char l[48]; std::snprintf(l, sizeof l, "winding=%u rc=%d dev row 3", wind, rc);
		DumpRow(*g, H - 4, l);
		delete g;
	}

	std::printf("Q11 FillPoint at (4.5,2.5)px\n");
	{
		RefGDraw* g = Fresh(false);
		POINT p = { INT32(4.5 * U), INT32(2.5 * U) };
		std::printf("  rc=%d\n", GDraw_FillPoint(g->Context(), &p));
		for (INT32 r = H - 1; r >= H - 5; r--) { char l[32]; std::snprintf(l, sizeof l, "device row %d", H - 1 - r); DumpRow(*g, r, l); }
		delete g;
	}

	std::printf("Q12 open path fill (implicitly closed?) triangle without CLOSEFIGURE\n");
	{
		RefGDraw* g = Fresh(false);
		POINT p[3] = { {1*U,0},{9*U,0},{1*U,8*U} };
		BYTE v[3] = { PT_MOVETO, PT_LINETO, PT_LINETO };
		std::printf("  rc=%d\n", GDraw_FillPath(g->Context(), p, v, 3, 0));
		DumpRow(*g, H - 3, "device row 2");
		delete g;
	}
	return 0;
}
