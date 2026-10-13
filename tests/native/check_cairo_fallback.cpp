// SPDX-License-Identifier: GPL-2.0-only
// Unsupported-operation handling for GDrawCairo (Stage 2A).
//
// Hybrid mirrors GDrawCairo's dispatch: every state call goes to CDraw and into
// XaraCairo::Gate/Renderer; a draw call is rendered by Cairo only if Gate::Blocker() is
// NULL and the core accepts it, otherwise CDraw draws it into the same bitmap.
//
// Each case checks that
//  - the unsupported operation was routed to CDraw with the expected reason,
//  - the surrounding supported operations were still drawn by Cairo,
//  - the hybrid image matches a CDraw-only rendering of the same sequence
//    (interiors/exteriors exact, AA edges within the differential-test tolerance,
//    pixels drawn by CDraw in both runs identical).
// Build/run: tests/native/run_cairo_differential.sh --fallback
#include "gdraw_reference.h"
#include "cairocore.h"
#include <cstdio>
#include <string>
#include <vector>

static const INT32 W = 64, H = 48, U = kRefUnitsPerPixel;
static const int kMaxEdgeDiff = 51;	// as check_cairo_differential.cpp
static int g_Failures = 0;

static void Check(bool Ok, const std::string& What)
{
	if (!Ok) { std::printf("  FAIL: %s\n", What.c_str()); g_Failures++; }
}

// Drawing sequence executed either by CDraw alone or by the hybrid dispatcher.
class Target
{
public:
	// Destination: 0 = BI_RGB on white; otherwise a transparency-channel (0x80000001) bitmap
	// filled with that background word.
	Target(bool Hybrid, bool TopDown, DWORD RGBTBackground = 0)
		: m_bHybrid(Hybrid),
		  m_Ref(W, H, TopDown, RGBTBackground ? RGBTBackground : 0x00ffffff, RGBTBackground ? 0x80000001 : BI_RGB)
	{
		m_Core.SetTarget(m_Ref.Bits(), W, H, TopDown, RGBTBackground != 0);
		m_Gate.Target(m_Core.HasTarget());
		Matrix(RefPixelMatrix());
		Antialias(true);
		GDraw_SetFlatness(m_Ref.Context(), U / 10);
		m_Core.SetFlatness(U / 10);
		Sync();
	}

	// state
	void Matrix(const GMATRIX& m)
	{
		GDraw_SetMatrix(m_Ref.Context(), &m);
		m_Core.SetMatrix({ m.AX, m.AY, m.BX, m.BY, m.CX, m.CY });
	}
	void Antialias(bool a) { GDraw_SetAntialiasFlag(m_Ref.Context(), a); m_Core.SetAntialias(a); }
	void Colour(COLORREF c)
	{
		GColour_SetColour(m_Ref.Context(), c);
		if (c >> 24) m_Gate.OtherPaint("colours carrying a transparency byte");
		else { m_Gate.SolidPaint(); m_Core.SetColour(c); }
	}
	void SolidColour(COLORREF c, UINT32 BPP)
	{
		GColour_SetSolidColour(m_Ref.Context(), c, BPP, 2);
		if ((c >> 24) || !XaraCairo::Gate::IsExactSolidDepth(BPP)) m_Gate.OtherPaint("solid colours quantised for a low-depth screen");
		else { m_Gate.SolidPaint(); m_Core.SetColour(c); }
	}
	void Transparency(COLORREF c, DWORD Style)
	{
		GColour_SetTransparency(m_Ref.Context(), c, Style);
		if (XaraCairo::Gate::IsOpaqueTransparency(c, Style)) { m_Gate.SolidPaint(); m_Core.SetColour(c & 0xffffff, Style == T_REFLECTIVE); }
		else m_Gate.OtherPaint("transparency");
	}
	void WordColour(DWORD c) { GColour_SetWordColour(m_Ref.Context(), c); m_Gate.OtherPaint("raw word colours"); }
	void Invert(DWORD c) { GColour_SetInvert(m_Ref.Context(), c); m_Gate.OtherPaint("inverted (XOR) paint"); }
	void LinearGradient(COLORREF a, COLORREF b, POINT pa, POINT pb)
	{
		// GDraw keeps a pointer to the table and reads it when filling, so it must outlive
		// this call (a stack-local, wrongly typed table without Length made the gradient
		// case render undefined pixels before Stage 2C).
		m_Grad.Length = GColour_GetGraduationTableLength();	// as GradTable::BuildTable does
		GColour_BuildGraduationTable(m_Ref.Context(), a, b, 0, &m_Grad);
		POINT pc = { pa.x - (pb.y - pa.y), pa.y + (pb.x - pa.x) };
		GColour_SetGraduation(m_Ref.Context(), GRAD_LINEAR, &m_Grad, &pa, &pb, &pc);
		m_Gate.OtherPaint("gradient fills");
	}
	void SeparationTables(bool On)
	{
		static BGR Table[256];
		for (int i = 0; i < 256; i++) Table[i] = { BYTE(i), BYTE(i), BYTE(i), 0 };
		static BYTE Ramp[256];
		for (int i = 0; i < 256; i++) Ramp[i] = BYTE(i);
		if (On) GColour_SetSeparationTables(m_Ref.Context(), Table, Table, Table, Table, Ramp, Ramp);
		else GColour_SetSeparationTables(m_Ref.Context(), NULL, NULL, NULL, NULL, NULL, NULL);
		m_Gate.SeparationTables(On);
	}
	void ClipRegionFromPath(const std::vector<POINT>& P, const std::vector<BYTE>& V)
	{
		m_Region.assign(64 * 1024, 0);
		GDraw_MakeRegion(m_Ref.Context(), P.data(), V.data(), UINT32(P.size()), 0, reinterpret_cast<REGION*>(m_Region.data()), UINT32(m_Region.size()));
		GDraw_ClipRegion(m_Ref.Context(), reinterpret_cast<REGION*>(m_Region.data()));
		m_Gate.RegionClip(true);
		Sync();
	}
	void ResetClip()
	{
		RECT Big = { INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN };
		GDraw_DeviceClipRectangle(m_Ref.Context(), &Big);
		m_Gate.RegionClip(false);
		Sync();
	}

	// drawing
	void FillPath(const std::vector<POINT>& P, const std::vector<BYTE>& V, DWORD Winding)
	{
		Draw("FillPath", [&] { return m_Core.FillPath(reinterpret_cast<const XaraCairo::DevicePoint*>(P.data()), V.data(), UINT32(P.size()), Winding); },
			 [&] { GDraw_FillPath(m_Ref.Context(), P.data(), V.data(), DWORD(P.size()), Winding); });
	}
	void StrokePath(const std::vector<POINT>& P, const std::vector<BYTE>& V, double WidthPx, const DashType* Dash = NULL)
	{
		const DWORD w = DWORD(WidthPx * U);
		const char* pPre = (Dash && Dash->Length) ? "dashed strokes" : NULL;
		Draw("StrokePath", [&] { return m_Core.StrokePath(reinterpret_cast<const XaraCairo::DevicePoint*>(P.data()), V.data(), UINT32(P.size()), false, w, XaraCairo::CAP_ROUND, XaraCairo::JOIN_ROUND); },
			 [&] { GDraw_StrokePath(m_Ref.Context(), P.data(), V.data(), DWORD(P.size()), false, w, CAPS_ROUND, JOIN_ROUND, Dash); }, pPre);
	}
	void FillRect(double x0, double y0, double x1, double y1)
	{
		RECT r = { INT32(x0 * U), INT32(y1 * U), INT32(x1 * U), INT32(y0 * U) };
		Draw("FillRectangle", [&] { return m_Core.FillRectangle({ r.left, r.top, r.right, r.bottom }); },
			 [&] { GDraw_FillRectangle(m_Ref.Context(), &r); });
	}

	const DWORD* Bits() { return m_Ref.Bits(); }
	int CairoDraws() const { return m_CairoDraws; }
	const std::vector<std::string>& Delegated() const { return m_Delegated; }

private:
	template <class C, class G>
	void Draw(const char* Op, C CairoDraw, G GDrawDraw, const char* pPreBlocker = NULL)
	{
		if (!m_bHybrid) { GDrawDraw(); return; }
		const char* pReason = m_Gate.Blocker();
		if (pReason == NULL) pReason = pPreBlocker;
		if (pReason == NULL)
		{
			if (CairoDraw() == XaraCairo::DRAWN) { m_CairoDraws++; return; }
			pReason = m_Core.LastUnsupportedReason();
		}
		m_Delegated.push_back(std::string(Op) + ": " + pReason);
		GDrawDraw();
	}
	void Sync()
	{
		RECT c;
		GDraw_GetDeviceClipRectangle(m_Ref.Context(), &c);
		m_Core.SetDeviceClip({ c.left, c.top, c.right, c.bottom });
	}

	bool m_bHybrid;
	RefGDraw m_Ref;
	XaraCairo::Renderer m_Core;
	XaraCairo::Gate m_Gate;
	std::vector<BYTE> m_Region;
	GraduationTable m_Grad = {};	// SetGraduation takes this type; GDraw keeps the pointer
	int m_CairoDraws = 0;
	std::vector<std::string> m_Delegated;
};

// ---- scenes -----------------------------------------------------------------------------

static std::vector<POINT> Pts(const std::vector<std::pair<double, double>>& p)
{
	std::vector<POINT> r;
	for (auto& q : p) r.push_back({ INT32(q.first * U), INT32(q.second * U) });
	return r;
}
static std::vector<BYTE> Closed(size_t n)
{
	std::vector<BYTE> v(n, PT_LINETO);
	v[0] = PT_MOVETO;
	v[n - 1] |= PT_CLOSEFIGURE;
	return v;
}
static const auto Tri = Pts({ {4, 4}, {30, 6}, {12, 40} });
static const auto Quad = Pts({ {34, 4}, {60, 4}, {60, 44}, {34, 44} });
static const auto Line = Pts({ {4, 44}, {60, 30} });
static const std::vector<BYTE> LineV = { PT_MOVETO, PT_LINETO };

struct Case
{
	const char* Name;
	const char* ExpectedReason;		// substring that must appear in a delegated entry
	void (*Run)(Target&);
};

static void Surround(Target& t)		// supported operations before/after every case
{
	t.Colour(RGB(20, 60, 200));
	t.FillPath(Tri, Closed(3), 0);
}

static const Case kCases[] = {
	{ "gradient fill", "FillPath: gradient fills", [](Target& t) {
		Surround(t); t.LinearGradient(RGB(255, 0, 0), RGB(0, 0, 255), { 34 * U, 0 }, { 60 * U, 0 }); t.FillPath(Quad, Closed(4), 0);
		t.Colour(RGB(0, 0, 0)); t.StrokePath(Line, LineV, 2); } },
	{ "50% transparency", "FillPath: transparency", [](Target& t) {
		Surround(t); t.Transparency(RGB(200, 0, 0) | (0x80u << 24), T_REFLECTIVE); t.FillPath(Quad, Closed(4), 0);
		t.Colour(RGB(0, 0, 0)); t.FillRect(2, 20, 40, 24); } },
	{ "T_NONE with transparency byte", "FillRectangle: transparency", [](Target& t) {
		Surround(t); t.Transparency(RGB(0, 200, 0) | (0x40u << 24), T_NONE); t.FillRect(34, 4, 60, 44);
		t.Transparency(RGB(0, 0, 0), T_REFLECTIVE); t.FillRect(2, 20, 40, 24); } },
	{ "inverse winding", "FillPath: inverse winding fill", [](Target& t) {
		Surround(t); t.Colour(RGB(240, 200, 0)); t.FillPath(Quad, Closed(4), 2); } },
	{ "dashed stroke", "StrokePath: dashed strokes", [](Target& t) {
		Surround(t); DashType d; std::memset(&d, 0, sizeof d); d.Length = 2; d.Array[0] = 4 * U; d.Array[1] = 2 * U;
		t.Colour(RGB(0, 0, 0)); t.StrokePath(Line, LineV, 2, &d); t.StrokePath(Pts({ {4, 2}, {60, 2} }), LineV, 1); } },
	{ "word colour", "FillPath: raw word colours", [](Target& t) {
		Surround(t); t.WordColour(0x7f00ffff); t.FillPath(Quad, Closed(4), 0); } },
	{ "XOR invert", "FillRectangle: inverted (XOR) paint", [](Target& t) {
		Surround(t); t.Invert(0x00ffffff); t.FillRect(8, 8, 50, 30); t.Colour(RGB(0, 128, 0)); t.FillRect(50, 30, 62, 46); } },
	{ "16bpp-quantised solid colour", "FillPath: solid colours quantised", [](Target& t) {
		Surround(t); t.SolidColour(RGB(0x12, 0x34, 0x56), 16); t.FillPath(Quad, Closed(4), 0);
		t.SolidColour(RGB(0x12, 0x34, 0x56), 32); t.FillRect(2, 20, 30, 24); } },
	{ "clip region then reset", "FillPath: clip regions", [](Target& t) {
		Surround(t); t.ClipRegionFromPath(Pts({ {0, 0}, {40, 0}, {20, 48} }), Closed(3));
		t.Colour(RGB(160, 0, 160)); t.FillPath(Quad, Closed(4), 0); t.FillPath(Pts({ {10, 10}, {38, 10}, {38, 38}, {10, 38} }), Closed(4), 1);
		t.ResetClip(); t.Colour(RGB(0, 0, 0)); t.FillRect(2, 44, 62, 46); } },
	{ "separation tables", "FillPath: colour separation tables", [](Target& t) {
		Surround(t); t.SeparationTables(true); t.Colour(RGB(10, 200, 30)); t.FillPath(Quad, Closed(4), 0);
		t.SeparationTables(false); t.Colour(RGB(0, 0, 0)); t.FillRect(2, 20, 30, 24); } },
	{ "rotated rectangle", "FillRectangle: rectangle under a rotating matrix", [](Target& t) {
		Surround(t); GMATRIX m = RefPixelMatrix(); m.AY = m.AX / 4; m.BX = -m.AY; t.Matrix(m);
		t.Colour(RGB(0, 120, 120)); t.FillRect(30, 2, 50, 20); t.Matrix(RefPixelMatrix()); t.FillRect(2, 20, 30, 24); } },
	{ "stroke under non-uniform matrix", "StrokePath: stroke under a non-uniform matrix", [](Target& t) {
		Surround(t); GMATRIX m = RefPixelMatrix(); m.BY = m.AX / 2; t.Matrix(m);
		t.Colour(RGB(0, 0, 0)); t.StrokePath(Pts({ {34, 10}, {60, 70} }), LineV, 2); } },
	// Mixed drawing into one destination: overlapping operations alternate between Cairo
	// and GDraw, so any ordering, flushing or compositing mismatch shows up as a pixel
	// difference against the CDraw-only rendering of the same sequence.
	{ "interleaved overlapping cairo/gdraw ordering", "FillPath: transparency", [](Target& t) {
		const auto Big = Pts({ {6, 6}, {58, 6}, {58, 42}, {6, 42} });
		const auto Diamond = Pts({ {32, 2}, {62, 24}, {32, 46}, {2, 24} });
		t.Colour(RGB(30, 90, 200)); t.FillPath(Big, Closed(4), 0);										// cairo
		t.Transparency(RGB(220, 40, 40) | (0x60u << 24), T_REFLECTIVE); t.FillPath(Diamond, Closed(4), 0);	// gdraw
		t.Colour(RGB(0, 0, 0)); t.StrokePath(Pts({ {2, 4}, {62, 44} }), LineV, 3);						// cairo over both
		t.LinearGradient(RGB(255, 255, 0), RGB(0, 160, 0), { 20 * U, 0 }, { 44 * U, 0 }); t.FillPath(Tri, Closed(3), 0);	// gdraw
		t.Colour(RGB(240, 240, 240)); t.FillRect(26.5, 18.25, 38.5, 30.75);								// cairo on top
		t.WordColour(0x7f00ffff); t.FillRect(30, 20, 34, 28);												// gdraw word write
		t.Colour(RGB(10, 10, 10)); t.FillPath(Pts({ {28, 10}, {36, 10}, {32, 38} }), Closed(3), 1); } },	// cairo last
	// Malformed paths are checked against the core only (see MalformedPathChecks): CDraw
	// itself loops forever in GDraw::DoFlattenCurve on a curve with missing points.
};

static int Cov(DWORD a, DWORD b)
{
	int d = 0;
	for (int s = 0; s < 32; s += 8) d = std::max(d, std::abs(int((a >> s) & 0xff) - int((b >> s) & 0xff)));
	return d;
}

static void RunCase(const Case& c, bool TopDown, DWORD RGBTBackground)
{
	if (RGBTBackground) std::printf("%s rgbt %08x %s\n", TopDown ? "td" : "bu", RGBTBackground, c.Name);
	else std::printf("%s %s\n", TopDown ? "td" : "bu", c.Name);
	Target Hybrid(true, TopDown, RGBTBackground), Reference(false, TopDown, RGBTBackground);
	c.Run(Hybrid);
	c.Run(Reference);

	bool Found = false;
	for (const std::string& d : Hybrid.Delegated())
	{
		std::printf("  delegated %s\n", d.c_str());
		Found |= d.find(c.ExpectedReason) != std::string::npos;
	}
	Check(Found, std::string("expected delegation '") + c.ExpectedReason + "'");
	Check(Hybrid.CairoDraws() > 0, "supported operations must still be drawn by Cairo");

	// Hybrid vs CDraw-only: same exact/edge rule as the differential test.
	long ExactDiff = 0, EdgeOver = 0;
	int MaxEdge = 0;
	for (int r = 0; r < H; r++)
		for (int x = 0; x < W; x++)
		{
			const DWORD a = Reference.Bits()[r * W + x], b = Hybrid.Bits()[r * W + x];
			bool Uniform = true;
			for (int j = -1; j <= 1 && Uniform; j++)
				for (int i = -1; i <= 1; i++)
				{
					const int xx = x + i, rr = r + j;
					if (xx < 0 || rr < 0 || xx >= W || rr >= H) continue;
					if (Reference.Bits()[rr * W + xx] != a) { Uniform = false; break; }
				}
			const int d = Cov(a, b);
			if (Uniform && d) ExactDiff++;
			if (!Uniform) { MaxEdge = std::max(MaxEdge, d); if (d > kMaxEdgeDiff) EdgeOver++; }
		}
	std::printf("  cairo draws %d, interior/exterior differences %ld, max edge diff %d\n", Hybrid.CairoDraws(), ExactDiff, MaxEdge);
	Check(ExactDiff == 0, "interior/exterior pixels must equal the CDraw-only rendering");
	Check(EdgeOver == 0, "edge pixels within tolerance of the CDraw-only rendering");
}

// Gate transitions that do not need pixels.
static void GateUnitChecks()
{
	std::printf("gate unit checks\n");
	XaraCairo::Gate g;
	Check(g.Blocker() && std::string(g.Blocker()).find("32bpp") != std::string::npos, "no target blocks");
	g.Target(true);
	Check(g.Blocker() && std::string(g.Blocker()).find("first colour") != std::string::npos, "paint before first colour blocks");
	g.SolidPaint();
	Check(g.Blocker() == NULL, "solid paint on a supported target draws");
	g.OtherPaint("gradient fills");
	Check(g.Blocker() && std::string(g.Blocker()) == "gradient fills", "other paint blocks");
	g.SolidPaint();
	Check(g.Blocker() == NULL, "a colour call replaces any earlier paint");
	g.TransparencyTable(true);
	Check(g.Blocker() != NULL, "transparency lookup table blocks");
	g.TransparencyTable(false);
	g.MatrixIntFlag(true);
	Check(g.Blocker() != NULL, "integer matrix mode blocks");
	g.MatrixIntFlag(false);
	g.ConversionTable(true);
	Check(g.Blocker() != NULL, "bitmap conversion table blocks");
	g.ConversionTable(false);
	Check(g.Blocker() == NULL, "clearing flags restores Cairo");
	Check(XaraCairo::Gate::IsOpaqueTransparency(0x00123456, T_NONE) && XaraCairo::Gate::IsOpaqueTransparency(0x00123456, T_REFLECTIVE),
		  "T_NONE/T_REFLECTIVE with zero byte are opaque");
	Check(!XaraCairo::Gate::IsOpaqueTransparency(0x01123456, T_NONE) && !XaraCairo::Gate::IsOpaqueTransparency(0x00123456, T_FLAT_REFLECTIVE),
		  "non-zero byte or other styles are not opaque");
	Check(XaraCairo::Gate::IsExactSolidDepth(32) && XaraCairo::Gate::IsExactSolidDepth(24) && XaraCairo::Gate::IsExactSolidDepth(0) &&
		  !XaraCairo::Gate::IsExactSolidDepth(16) && !XaraCairo::Gate::IsExactSolidDepth(8), "solid colour depths");
}

// The core must refuse malformed paths (so GDrawCairo hands them to GDraw unchanged,
// preserving the default renderer's behaviour) and must not write any pixel.
static void MalformedPathChecks()
{
	std::printf("malformed path checks\n");
	std::vector<DWORD> Bits(size_t(W) * H, 0x00ffffff);
	XaraCairo::Renderer r;
	r.SetTarget(Bits.data(), W, H, false);
	const GMATRIX m = RefPixelMatrix();
	r.SetMatrix({ m.AX, m.AY, m.BX, m.BY, m.CX, m.CY });
	r.SetColour(0);
	const auto p = Pts({ {34, 4}, {60, 4}, {60, 44}, {30, 30} });
	const std::vector<std::vector<BYTE>> Bad = {
		{ PT_MOVETO, PT_BEZIERTO, PT_BEZIERTO },						// curve missing a point
		{ PT_LINETO, PT_LINETO, PT_LINETO },							// no initial move
		{ PT_MOVETO, PT_BEZIERTO, BYTE(PT_BEZIERTO | PT_CLOSEFIGURE), PT_BEZIERTO },	// close inside curve
		{ BYTE(PT_MOVETO | PT_CLOSEFIGURE), PT_LINETO },				// closed move
		{ PT_MOVETO, 8 },												// unknown verb
		{} };															// empty
	for (const auto& v : Bad)
	{
		const XaraCairo::Result Fill = r.FillPath(reinterpret_cast<const XaraCairo::DevicePoint*>(p.data()), v.empty() ? NULL : v.data(), UINT32(v.size()), 0);
		Check(Fill == XaraCairo::UNSUPPORTED && std::string(r.LastUnsupportedReason()) == "path structure not recognised", "malformed fill rejected");
		const XaraCairo::Result Stroke = r.StrokePath(reinterpret_cast<const XaraCairo::DevicePoint*>(p.data()), v.empty() ? NULL : v.data(), UINT32(v.size()), false, U, XaraCairo::CAP_BUTT, XaraCairo::JOIN_MITER);
		Check(Stroke == XaraCairo::UNSUPPORTED, "malformed stroke rejected");
	}
	bool Untouched = true;
	for (DWORD px : Bits) Untouched &= (px == 0x00ffffff);
	Check(Untouched, "rejected paths write no pixels");
	const XaraCairo::DeviceRect b = r.GetChangedBBox();
	Check(b.left > b.right, "rejected paths leave the changed box empty");
}

int main()
{
	GateUnitChecks();
	MalformedPathChecks();
	for (const Case& c : kCases)
		for (int TopDown = 0; TopDown <= 1; TopDown++)
			for (DWORD Background : { 0u, 0xff000000u, 0x80404040u })
				RunCase(c, TopDown != 0, Background);
	std::printf("%s: %zu fallback cases x 2 orientations x 3 destinations (BI_RGB, transparent and 50%% transparent RGBT), %d check failures\n",
				g_Failures ? "FAIL" : "PASS", sizeof(kCases) / sizeof(kCases[0]), g_Failures);
	return g_Failures ? 1 : 0;
}
