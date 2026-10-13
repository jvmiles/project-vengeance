// SPDX-License-Identifier: GPL-2.0-only
// Differential test: identical drawing requests to CDraw (reference, public API) and to
// the GDrawCairo rasterisation core (Kernel/cairocore.cpp), compared pixel by pixel.
//
// Build/run: tests/native/run_cairo_differential.sh
//
// Per scene (black on white, so a channel value gives coverage directly):
//  - exact:   pixels whose 3x3 reference neighbourhood is entirely uncovered or entirely
//             covered must be identical (interiors, exteriors, colour mapping);
//  - edge:    antialiased: remaining pixels may differ by at most kMaxEdgeDiff levels,
//             one CDraw sub-scanline (CDraw samples 5 rows per pixel, so a coverage
//             step is 0.2 = 51 levels; Cairo uses exact area). Non-antialiased:
//             whole-pixel flips are allowed only on edge pixels, at most
//             kMaxBinaryFlips of them (exact centre ties on slanted edges, flattening);
//  - area:    total coverage within kMaxAreaError of the reference, or for thin shapes
//             a mean bias of at most kMaxEdgeBias coverage per edge pixel;
//  - format:  the top byte must match on every pixel whose colour matches, unless a
//             pixel within 3 columns on the same row differs in colour (CDraw's AA
//             span path writes whole 4-pixel groups, so a 1-pixel coverage difference
//             at a run end can move a group boundary). Scenes run on backgrounds with
//             top byte 0x00 and 0x40;
//  - bbox:    the changed box must contain every pixel Cairo changed, and lie within
//             one pixel of the reference changed box on each side.
// Scenes with a documented, explained difference are marked Known: they are reported
// as "known" with the reason and still must meet the exact/format/bbox checks.
#include "gdraw_reference.h"
#include "cairocore.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

static_assert(sizeof(XaraCairo::DevicePoint) == sizeof(POINT), "POINT layout");
static_assert(sizeof(XaraCairo::DeviceRect) == sizeof(RECT), "RECT layout");
static_assert(sizeof(XaraCairo::DeviceMatrix) == sizeof(GMATRIX), "GMATRIX layout");

static const INT32 W = 64, H = 48, U = kRefUnitsPerPixel;
static const int kMaxEdgeDiff = 51;			// AA: one CDraw sub-scanline (1/5 pixel coverage)
static const double kMaxAreaError = 0.03;	// fraction of the reference coverage
static const double kMaxEdgeBias = 0.10;	// mean coverage bias per edge pixel (one sub-scanline)
static const double kMaxBinaryFlips = 0.05;	// non-AA: fraction of edge pixels

static bool g_Verbose = false;
static int g_Known = 0;

// One backend-neutral drawing step.
struct Ops
{
	std::function<void(GCONTEXT*)> Ref;
	std::function<void(XaraCairo::Renderer&, GCONTEXT*)> Cairo;
};

struct Scene
{
	std::string Name;
	GMATRIX Matrix;
	bool Antialias;
	bool Hinting;
	std::vector<Ops> Steps;
	bool ExpectExact;		// every pixel must match (non-AA rectangles/points)
	std::string Known;		// non-empty: documented difference, reported not failed
};

// ---- builders ------------------------------------------------------------------------

struct PathData { std::vector<POINT> P; std::vector<BYTE> V; };

static INT32 Units(double px) { return INT32(std::lround(px * U)); }

static PathData Polygon(const std::vector<std::pair<double, double>>& Pts, bool Close = true)
{
	PathData d;
	for (size_t i = 0; i < Pts.size(); i++)
	{
		d.P.push_back({ Units(Pts[i].first), Units(Pts[i].second) });
		d.V.push_back(i == 0 ? PT_MOVETO : PT_LINETO);
	}
	if (Close) d.V.back() |= PT_CLOSEFIGURE;
	return d;
}

static void AppendEllipse(PathData& d, double cx, double cy, double rx, double ry)
{
	const double k = 0.5522847498;
	const double pts[13][2] = {
		{cx + rx, cy}, {cx + rx, cy + k * ry}, {cx + k * rx, cy + ry}, {cx, cy + ry},
		{cx - k * rx, cy + ry}, {cx - rx, cy + k * ry}, {cx - rx, cy}, {cx - rx, cy - k * ry},
		{cx - k * rx, cy - ry}, {cx, cy - ry}, {cx + k * rx, cy - ry}, {cx + rx, cy - k * ry}, {cx + rx, cy} };
	for (int i = 0; i < 13; i++)
	{
		d.P.push_back({ Units(pts[i][0]), Units(pts[i][1]) });
		d.V.push_back(i == 0 ? PT_MOVETO : PT_BEZIERTO);
	}
	d.V.back() |= PT_CLOSEFIGURE;
}

static Ops Colour(COLORREF c)
{
	return { [c](GCONTEXT* g) { GColour_SetColour(g, c); },
			 [c](XaraCairo::Renderer& r, GCONTEXT*) { r.SetColour(c); } };
}

// SetTransparency(T_REFLECTIVE) with a zero byte: opaque, but GDraw keeps the top byte.
static Ops Reflective(COLORREF c)
{
	return { [c](GCONTEXT* g) { GColour_SetTransparency(g, c, T_REFLECTIVE); },
			 [c](XaraCairo::Renderer& r, GCONTEXT*) { r.SetColour(c, true); } };
}

static Ops FillRect(double x0, double y0, double x1, double y1)
{
	RECT rc = { Units(x0), Units(y1), Units(x1), Units(y0) };
	return { [rc](GCONTEXT* g) { GDraw_FillRectangle(g, &rc); },
			 [rc](XaraCairo::Renderer& r, GCONTEXT*) {
				 XaraCairo::DeviceRect d = { rc.left, rc.top, rc.right, rc.bottom };
				 r.FillRectangle(d); } };
}

static Ops FillPoint(double x, double y)
{
	POINT p = { Units(x), Units(y) };
	return { [p](GCONTEXT* g) { GDraw_FillPoint(g, &p); },
			 [p](XaraCairo::Renderer& r, GCONTEXT*) { r.FillPoint({ p.x, p.y }); } };
}

static Ops Fill(const PathData& d, DWORD Winding)
{
	return { [d, Winding](GCONTEXT* g) { GDraw_FillPath(g, d.P.data(), d.V.data(), DWORD(d.P.size()), Winding); },
			 [d, Winding](XaraCairo::Renderer& r, GCONTEXT*) {
				 r.FillPath(reinterpret_cast<const XaraCairo::DevicePoint*>(d.P.data()), d.V.data(), UINT32(d.P.size()), Winding); } };
}

static Ops Stroke(const PathData& d, double WidthPx, CapStyles Cap, JoinStyles Join)
{
	const DWORD w = DWORD(std::lround(WidthPx * U));
	return { [d, w, Cap, Join](GCONTEXT* g) { GDraw_StrokePath(g, d.P.data(), d.V.data(), DWORD(d.P.size()), false, w, Cap, Join, NULL); },
			 [d, w, Cap, Join](XaraCairo::Renderer& r, GCONTEXT*) {
				 r.StrokePath(reinterpret_cast<const XaraCairo::DevicePoint*>(d.P.data()), d.V.data(), UINT32(d.P.size()),
							  false, w, XaraCairo::CapStyle(Cap), XaraCairo::JoinStyle(Join)); } };
}

// Clip changes are applied to CDraw, then the resulting device clip is read back and
// given to the core: exactly what GDrawCairo does.
static void SyncClip(XaraCairo::Renderer& r, GCONTEXT* g)
{
	RECT c;
	GDraw_GetDeviceClipRectangle(g, &c);
	r.SetDeviceClip({ c.left, c.top, c.right, c.bottom });
}

static Ops DeviceClip(INT32 l, INT32 t, INT32 rr, INT32 b)
{
	RECT c = { l, t, rr, b };
	return { [c](GCONTEXT* g) { GDraw_DeviceClipRectangle(g, &c); },
			 [](XaraCairo::Renderer& r, GCONTEXT* g) { SyncClip(r, g); } };
}

static Ops ClipRect(double x0, double y0, double x1, double y1)
{
	RECT c = { Units(x0), Units(y1), Units(x1), Units(y0) };
	return { [c](GCONTEXT* g) { GDraw_ClipRectangle(g, &c); },
			 [](XaraCairo::Renderer& r, GCONTEXT* g) { SyncClip(r, g); } };
}

static GMATRIX Similarity(double PixelsPerUnitScale, double DegreesCCW, double OxPx, double OyPx)
{
	const double a = PixelsPerUnitScale * (1 << 20);
	const double t = DegreesCCW * M_PI / 180.0;
	GMATRIX m;
	m.AX = INT32(std::lround(a * std::cos(t)));
	m.AY = INT32(std::lround(a * std::sin(t)));
	m.BX = -m.AY;
	m.BY = m.AX;
	m.CX = __int64(std::llround(OxPx * double(1LL << 30)));
	m.CY = __int64(std::llround(OyPx * double(1LL << 30)));
	return m;
}

// ---- comparison ----------------------------------------------------------------------

struct Stats
{
	long Exact = 0, ExactDiff = 0, Edge = 0, EdgeDiff = 0, TopByteDiff = 0;
	int MaxEdgeDiff = 0;
	double RefArea = 0, CairoArea = 0;
	bool BBoxCovers = true;
	int BBoxSlack = 0;
	bool Pass = true;
	std::string Why;
};

static int Cov(DWORD p) { return 255 - int(p & 0xff); }		// black on white, blue channel

static Stats Compare(const Scene& s, bool TopDown, DWORD Background, RefGDraw& ref, const std::vector<DWORD>& cairo, const XaraCairo::DeviceRect& cbox)
{
	Stats st;
	auto RefCov = [&](int x, int r) { return Cov(ref.Pixel(x, r)); };
	RECT rbox;
	GDraw_GetChangedBBox(ref.Context(), &rbox);
	XaraCairo::DeviceRect touched = XaraCairo::Renderer::EmptyRect();
	for (int r = 0; r < H; r++)
		for (int x = 0; x < W; x++)
		{
			const DWORD a = ref.Pixel(x, r), b = cairo[size_t(r) * W + x];
			// Top byte: see the format rule in the header comment.
			if ((a >> 24) != (b >> 24))
			{
				bool NearColourDiff = false;
				for (int xx = std::max(0, x - 3); xx <= std::min(W - 1, x + 3); xx++)
					NearColourDiff |= (ref.Pixel(xx, r) & 0xffffff) != (cairo[size_t(r) * W + xx] & 0xffffff);
				if (!NearColourDiff) st.TopByteDiff++;
			}
			if (b != Background)
			{
				const int dy = TopDown ? H - 1 - r : r;
				touched = XaraCairo::Renderer::UnionRect(touched, { x, dy, x + 1, dy + 1 });
			}
			st.RefArea += RefCov(x, r) / 255.0;
			st.CairoArea += Cov(b) / 255.0;
			bool allZero = true, allFull = true;
			for (int j = -1; j <= 1; j++)
				for (int i = -1; i <= 1; i++)
				{
					const int xx = x + i, rr = r + j;
					if (xx < 0 || rr < 0 || xx >= W || rr >= H) continue;
					const int c = RefCov(xx, rr);
					allZero &= (c == 0);
					allFull &= (c == 255);
				}
			int d = 0;
			for (int sh = 0; sh < 24; sh += 8)
				d = std::max(d, std::abs(int((a >> sh) & 0xff) - int((b >> sh) & 0xff)));
			if (allZero || allFull)
			{
				st.Exact++;
				if (d) st.ExactDiff++;
			}
			else
			{
				st.Edge++;
				if (d) st.EdgeDiff++;
				st.MaxEdgeDiff = std::max(st.MaxEdgeDiff, d);
			}
		}
	// Changed box: must contain all pixels Cairo changed (cairo starts on white)
	if (touched.left < touched.right)
		st.BBoxCovers = cbox.left <= touched.left && cbox.right >= touched.right &&
						cbox.top <= touched.top && cbox.bottom >= touched.bottom;
	if (rbox.left <= rbox.right && cbox.left <= cbox.right)
		st.BBoxSlack = std::max(std::max(std::abs(rbox.left - cbox.left), std::abs(rbox.right - cbox.right)),
								std::max(std::abs(rbox.top - cbox.top), std::abs(rbox.bottom - cbox.bottom)));
	else if ((rbox.left <= rbox.right) != (cbox.left <= cbox.right))
		st.BBoxSlack = 99;	// one changed something, the other nothing

	const double AreaErr = st.RefArea > 0 ? std::fabs(st.CairoArea - st.RefArea) / st.RefArea : (st.CairoArea > 0 ? 1 : 0);
	const double EdgeBias = st.Edge ? std::fabs(st.CairoArea - st.RefArea) / st.Edge : 0;
	auto Fail = [&](const std::string& w) { st.Pass = false; st.Why += (st.Why.empty() ? "" : "; ") + w; };
	// Hard requirements, also for Known scenes.
	if (s.ExpectExact && (st.ExactDiff || st.EdgeDiff)) Fail("expected identical pixels");
	if (st.ExactDiff) Fail("interior/exterior differs");
	if (st.TopByteDiff) Fail("top byte differs");
	if (!st.BBoxCovers) Fail("changed box misses changed pixels");
	if (st.BBoxSlack > 1) Fail("changed box differs from reference");
	// Edge tolerances: a Known scene reports instead of failing.
	std::string Tol;
	if (s.Antialias && st.MaxEdgeDiff > kMaxEdgeDiff) Tol = "edge difference too large";
	if (!s.Antialias && st.EdgeDiff > kMaxBinaryFlips * st.Edge) Tol = "too many edge pixel flips";
	if (AreaErr > kMaxAreaError && EdgeBias > kMaxEdgeBias) Tol += (Tol.empty() ? "" : "; ") + std::string("area error");
	if (!Tol.empty())
	{
		if (s.Known.empty()) Fail(Tol);
		else st.Why += (st.Why.empty() ? "" : "; ") + ("known: " + s.Known + " (" + Tol + ")");
	}
	return st;
}

// FNV-1a over the Cairo output and its changed box: lets two core versions be compared
// for bit-identical results (tests/native/compare_cairo_hashes.sh).
static unsigned long long OutputHash(const std::vector<DWORD>& Px, const XaraCairo::DeviceRect& Box)
{
	unsigned long long h = 1469598103934665603ULL;
	auto Mix = [&](const void* p, size_t n) { for (size_t i = 0; i < n; i++) { h ^= static_cast<const unsigned char*>(p)[i]; h *= 1099511628211ULL; } };
	Mix(Px.data(), Px.size() * sizeof(DWORD));
	Mix(&Box, sizeof(Box));
	return h;
}

static bool RunScene(const Scene& s, bool TopDown, DWORD Background, FILE* pCsv)
{
	const std::string Label = std::string(TopDown ? "td" : "bu") + (Background >> 24 ? "+t " : "   ") + s.Name;
	// Flatness as GRenderRegion::SetQualityLevel: half a pixel in document units, /5 with AA.
	const double PixelUnits = double(1LL << 30) / std::sqrt(std::fabs(double(s.Matrix.AX) * s.Matrix.BY - double(s.Matrix.AY) * s.Matrix.BX));
	const UINT32 Flatness = UINT32(PixelUnits / 2 / (s.Antialias ? 5 : 1));

	RefGDraw ref(W, H, TopDown, Background);
	GDraw_SetMatrix(ref.Context(), &s.Matrix);
	GDraw_SetAntialiasFlag(ref.Context(), s.Antialias);
	GDraw_SetHintingFlag(ref.Context(), s.Hinting);
	GDraw_SetFlatness(ref.Context(), Flatness);
	GDraw_ClearChangedBBox(ref.Context());

	std::vector<DWORD> cairo(size_t(W) * H, Background);
	XaraCairo::Renderer r;
	r.SetTarget(cairo.data(), W, H, TopDown);
	r.SetMatrix({ s.Matrix.AX, s.Matrix.AY, s.Matrix.BX, s.Matrix.BY, s.Matrix.CX, s.Matrix.CY });
	r.SetAntialias(s.Antialias);
	r.SetHinting(s.Hinting);
	r.SetFlatness(Flatness);
	SyncClip(r, ref.Context());

	for (const Ops& o : s.Steps)
	{
		o.Ref(ref.Context());
		o.Cairo(r, ref.Context());
	}
	Stats st = Compare(s, TopDown, Background, ref, cairo, r.GetChangedBBox());
	const double AreaErr = st.RefArea > 0 ? (st.CairoArea - st.RefArea) / st.RefArea : 0;
	if (st.Pass && !st.Why.empty()) g_Known++;
	std::printf("%-5s %-49s exact %5ld/%-5ld edge %4ld/%-4ld maxEdge %3d area %+6.2f%% bbox %s%d %s\n",
				!st.Pass ? "FAIL" : (st.Why.empty() ? "ok" : "known"), Label.c_str(), st.ExactDiff, st.Exact, st.EdgeDiff, st.Edge,
				st.MaxEdgeDiff, AreaErr * 100, st.BBoxCovers ? "" : "MISS ", st.BBoxSlack, st.Why.c_str());
	if (pCsv)
		std::fprintf(pCsv, "%s,%d,%ld,%ld,%ld,%ld,%d,%.5f,%d,%d,%ld,%016llx\n", Label.c_str(), st.Pass, st.ExactDiff, st.Exact,
					 st.EdgeDiff, st.Edge, st.MaxEdgeDiff, AreaErr, st.BBoxCovers, st.BBoxSlack, st.TopByteDiff,
					 OutputHash(cairo, r.GetChangedBBox()));
	if (g_Verbose && !st.Pass)
	{
		RECT rb;
		GDraw_GetChangedBBox(ref.Context(), &rb);
		const XaraCairo::DeviceRect cb = r.GetChangedBBox();
		std::printf("changed box: reference %d %d %d %d, cairo %d %d %d %d\n",
					rb.left, rb.top, rb.right, rb.bottom, cb.left, cb.top, cb.right, cb.bottom);
	}
	if (g_Verbose && !st.Pass)
		for (int row = 0; row < H; row++)
		{
			for (int x = 0; x < W; x++)
			{
				const int a = Cov(ref.Pixel(x, row)), b = Cov(cairo[size_t(row) * W + x]);
				// '#' both, 'R'/'C' only (or much more) in reference/Cairo, 'r'/'c' small excess
				const bool Big = std::abs(a - b) > kMaxEdgeDiff;
				std::putchar(a == b ? (a ? '#' : '.') : (a > b ? (Big ? 'R' : 'r') : (Big ? 'C' : 'c')));
			}
			std::putchar('\n');
		}
	return st.Pass;
}

// ---- transparency-channel (0x80000001) destinations -----------------------------------
//
// The destination is the editor's capture format: top byte T = 255 - alpha, colour
// premultiplied. Backgrounds: fully transparent (as GDrawAsm::SetupBitmap initialises
// captures), opaque white, and a per-pixel mix of partial transparencies. Pixels are
// classified from two CDraw-only BI_RGB runs of the same scene (white and black
// background): untouched if both show their background, fully covered if both agree,
// otherwise partial. Interiors/exteriors (uniform 3x3 class) must match exactly in all
// four channels; other pixels follow the AA / non-AA edge rules of the BI_RGB test.

enum { RGBT_TRANSPARENT, RGBT_OPAQUE_WHITE, RGBT_MIXED, RGBT_KINDS };
static const char* const kRGBTNames[] = { "rgbt-clear", "rgbt-white", "rgbt-mixed" };

static DWORD RGBTBackground(int Kind, int x, int Row)
{
	if (Kind == RGBT_TRANSPARENT) return 0xff000000;
	if (Kind == RGBT_OPAQUE_WHITE) return 0x00ffffff;
	const DWORD T = DWORD((x * 37 + Row * 91) % 256), A = 255 - T;	// premultiplied: channel <= alpha
	const DWORD R = A * DWORD((x * 7) % 256) / 255, G = A * DWORD((Row * 13) % 256) / 255, B = A * DWORD(((x + Row) * 5) % 256) / 255;
	return (T << 24) | (R << 16) | (G << 8) | B;
}

static void SetupRef(const Scene& s, RefGDraw& g, UINT32 Flatness)
{
	GDraw_SetMatrix(g.Context(), &s.Matrix);
	GDraw_SetAntialiasFlag(g.Context(), s.Antialias);
	GDraw_SetHintingFlag(g.Context(), s.Hinting);
	GDraw_SetFlatness(g.Context(), Flatness);
	GDraw_ClearChangedBBox(g.Context());
}

static bool RunSceneRGBT(const Scene& s, bool TopDown, int Kind, FILE* pCsv)
{
	const std::string Label = std::string(TopDown ? "td " : "bu ") + kRGBTNames[Kind] + " " + s.Name;
	const double PixelUnits = double(1LL << 30) / std::sqrt(std::fabs(double(s.Matrix.AX) * s.Matrix.BY - double(s.Matrix.AY) * s.Matrix.BX));
	const UINT32 Flatness = UINT32(PixelUnits / 2 / (s.Antialias ? 5 : 1));

	RefGDraw ref(W, H, TopDown, 0, 0x80000001);
	std::vector<DWORD> bg(size_t(W) * H);
	for (int r = 0; r < H; r++)
		for (int x = 0; x < W; x++)
			bg[size_t(r) * W + x] = ref.Bits()[size_t(r) * W + x] = RGBTBackground(Kind, x, r);
	SetupRef(s, ref, Flatness);

	std::vector<DWORD> cairo = bg;
	XaraCairo::Renderer core;
	core.SetTarget(cairo.data(), W, H, TopDown, true);
	core.SetMatrix({ s.Matrix.AX, s.Matrix.AY, s.Matrix.BX, s.Matrix.BY, s.Matrix.CX, s.Matrix.CY });
	core.SetAntialias(s.Antialias);
	core.SetHinting(s.Hinting);
	core.SetFlatness(Flatness);
	SyncClip(core, ref.Context());
	for (const Ops& o : s.Steps)
	{
		o.Ref(ref.Context());
		o.Cairo(core, ref.Context());
	}

	// Classification runs (CDraw only).
	RefGDraw OnWhite(W, H, TopDown, 0x00ffffff), OnBlack(W, H, TopDown, 0x00000000);
	for (RefGDraw* g : { &OnWhite, &OnBlack })
	{
		SetupRef(s, *g, Flatness);
		for (const Ops& o : s.Steps)
			o.Ref(g->Context());
	}
	auto Class = [&](int x, int r)		// 0 untouched, 2 fully covered, 1 partial
	{
		const DWORD w = OnWhite.Pixel(x, r) & 0xffffff, b = OnBlack.Pixel(x, r) & 0xffffff;
		return (w == 0xffffff && b == 0) ? 0 : (w == b ? 2 : 1);
	};

	Stats st;
	XaraCairo::DeviceRect touched = XaraCairo::Renderer::EmptyRect();
	for (int r = 0; r < H; r++)
		for (int x = 0; x < W; x++)
		{
			const DWORD a = ref.Pixel(x, r), b = cairo[size_t(r) * W + x], g = bg[size_t(r) * W + x];
			if (b != g)
			{
				const int dy = TopDown ? H - 1 - r : r;
				touched = XaraCairo::Renderer::UnionRect(touched, { x, dy, x + 1, dy + 1 });
			}
			if (Kind == RGBT_TRANSPARENT)		// coverage = 1 - T / 255
			{
				st.RefArea += (255 - int(a >> 24)) / 255.0;
				st.CairoArea += (255 - int(b >> 24)) / 255.0;
			}
			const int c = Class(x, r);
			bool Uniform = (c != 1);
			for (int j = -1; j <= 1 && Uniform; j++)
				for (int i = -1; i <= 1; i++)
				{
					const int xx = x + i, rr = r + j;
					if (xx < 0 || rr < 0 || xx >= W || rr >= H) continue;
					if (Class(xx, rr) != c) { Uniform = false; break; }
				}
			int d = 0;
			for (int sh = 0; sh < 32; sh += 8)
				d = std::max(d, std::abs(int((a >> sh) & 0xff) - int((b >> sh) & 0xff)));
			if (Uniform)
			{
				st.Exact++;
				if (d) st.ExactDiff++;
			}
			else
			{
				st.Edge++;
				if (d) st.EdgeDiff++;
				st.MaxEdgeDiff = std::max(st.MaxEdgeDiff, d);
			}
		}
	RECT rbox;
	GDraw_GetChangedBBox(ref.Context(), &rbox);
	const XaraCairo::DeviceRect cbox = core.GetChangedBBox();
	if (touched.left < touched.right)
		st.BBoxCovers = cbox.left <= touched.left && cbox.right >= touched.right &&
						cbox.top <= touched.top && cbox.bottom >= touched.bottom;
	if (rbox.left <= rbox.right && cbox.left <= cbox.right)
		st.BBoxSlack = std::max(std::max(std::abs(rbox.left - cbox.left), std::abs(rbox.right - cbox.right)),
								std::max(std::abs(rbox.top - cbox.top), std::abs(rbox.bottom - cbox.bottom)));
	else if ((rbox.left <= rbox.right) != (cbox.left <= cbox.right))
		st.BBoxSlack = 99;

	const double AreaErr = st.RefArea > 0 ? std::fabs(st.CairoArea - st.RefArea) / st.RefArea : 0;
	const double EdgeBias = st.Edge ? std::fabs(st.CairoArea - st.RefArea) / st.Edge : 0;
	auto Fail = [&](const std::string& w) { st.Pass = false; st.Why += (st.Why.empty() ? "" : "; ") + w; };
	if (s.ExpectExact && (st.ExactDiff || st.EdgeDiff)) Fail("expected identical pixels");
	if (st.ExactDiff) Fail("interior/exterior differs (any channel)");
	if (!st.BBoxCovers) Fail("changed box misses changed pixels");
	if (st.BBoxSlack > 1) Fail("changed box differs from reference");
	std::string Tol;
	if (s.Antialias && st.MaxEdgeDiff > kMaxEdgeDiff) Tol = "edge difference too large";
	if (!s.Antialias && st.EdgeDiff > kMaxBinaryFlips * st.Edge) Tol = "too many edge pixel flips";
	if (Kind == RGBT_TRANSPARENT && AreaErr > kMaxAreaError && EdgeBias > kMaxEdgeBias)
		Tol += (Tol.empty() ? "" : "; ") + std::string("area error");
	if (!Tol.empty())
	{
		if (s.Known.empty()) Fail(Tol);
		else st.Why += (st.Why.empty() ? "" : "; ") + ("known: " + s.Known + " (" + Tol + ")");
	}

	if (st.Pass && !st.Why.empty()) g_Known++;
	std::printf("%-5s %-49s exact %5ld/%-5ld edge %4ld/%-4ld maxEdge %3d area %+6.2f%% bbox %s%d %s\n",
				!st.Pass ? "FAIL" : (st.Why.empty() ? "ok" : "known"), Label.c_str(), st.ExactDiff, st.Exact, st.EdgeDiff, st.Edge,
				st.MaxEdgeDiff, (Kind == RGBT_TRANSPARENT ? (st.CairoArea - st.RefArea) / std::max(st.RefArea, 1e-9) : 0) * 100,
				st.BBoxCovers ? "" : "MISS ", st.BBoxSlack, st.Why.c_str());
	if (pCsv)
		std::fprintf(pCsv, "%s,%d,%ld,%ld,%ld,%ld,%d,%.5f,%d,%d,%d,%016llx\n", Label.c_str(), st.Pass, st.ExactDiff, st.Exact,
					 st.EdgeDiff, st.Edge, st.MaxEdgeDiff, AreaErr, st.BBoxCovers, st.BBoxSlack, 0, OutputHash(cairo, cbox));
	return st.Pass;
}

// ---- scenes --------------------------------------------------------------------------

static std::vector<Scene> BuildScenes()
{
	std::vector<Scene> v;
	const GMATRIX Pixel = RefPixelMatrix();
	const COLORREF Black = RGB(0, 0, 0);
	auto Add = [&](const std::string& n, GMATRIX m, bool aa, bool hint, std::vector<Ops> steps, bool exact = false)
	{
		steps.insert(steps.begin(), Colour(Black));
		v.push_back({ n, m, aa, hint, steps, exact, "" });
	};

	// Rectangles and points: CDraw snaps to whole pixels, never antialiases.
	for (int aa = 0; aa <= 1; aa++)
	{
		std::mt19937 rng(11 + aa);
		std::uniform_real_distribution<double> X(-4, W + 4), Y(-4, H + 4);
		std::vector<Ops> rects;
		for (int i = 0; i < 40; i++) rects.push_back(FillRect(X(rng), Y(rng), X(rng), Y(rng)));
		Add(std::string("rects random 40 aa=") + char('0' + aa), Pixel, aa, false, rects, true);
		std::vector<Ops> ties;
		for (int i = 0; i < 16; i++) ties.push_back(FillRect(2 + i * 3 + 0.5, 1 + i * 0.5, 4 + i * 3 + 0.5, 30 + i * 0.25));
		Add(std::string("rects half-pixel ties aa=") + char('0' + aa), Pixel, aa, false, ties, true);
		std::vector<Ops> pts;
		for (int i = 0; i < 60; i++) pts.push_back(FillPoint(X(rng), Y(rng)));
		for (int i = 0; i < 8; i++) pts.push_back(FillPoint(5.5 + i * 7, 10.5));
		Add(std::string("points random+ties aa=") + char('0' + aa), Pixel, aa, false, pts, true);
	}
	Add("rects scaled 0.37 offset 0.3", Similarity(0.37, 0, 0.3, 0.7), false, false,
		{ FillRect(10, 10, 100, 90), FillRect(-20, 30, 50, 120) }, true);
	Add("rects negative origin", Similarity(1, 0, -7.5, -3.5), false, false,
		{ FillRect(5.25, 5.75, 40.5, 30.5), FillRect(-3, -3, 12.5, 9.5) }, true);
	{
		std::vector<Ops> cols;
		const COLORREF c[] = { RGB(0x12, 0x34, 0x56), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(1, 2, 3), RGB(254, 253, 252) };
		for (int i = 0; i < 6; i++) { cols.push_back(Colour(c[i])); cols.push_back(FillRect(i * 10 + 1, 2, i * 10 + 9, 40)); }
		v.push_back({ "solid colour mapping", Pixel, false, false, cols, true, "" });
	}

	// Path fills.
	for (int aa = 0; aa <= 1; aa++)
	{
		const std::string A = std::string(" aa=") + char('0' + aa);
		Add("poly axis-aligned fractional" + A, Pixel, aa, false,
			{ Fill(Polygon({ {2.25, 2.5}, {30.75, 2.5}, {30.75, 20.3}, {2.25, 20.3} }), 0),
			  Fill(Polygon({ {34.5, 2.5}, {60.5, 2.5}, {60.5, 40.5}, {34.5, 40.5} }), 0) });
		Add("poly triangle + star" + A, Pixel, aa, false,
			{ Fill(Polygon({ {3, 3}, {40, 9}, {12, 44} }), 0),
			  Fill(Polygon({ {50, 2}, {54, 14}, {63, 14}, {56, 21}, {59, 33}, {50, 26}, {41, 33}, {44, 21}, {37, 14}, {46, 14} }), 1) });
		Add("winding overlap even-odd" + A, Pixel, aa, false,
			{ Fill([] { PathData d = Polygon({ {4, 4}, {34, 4}, {34, 34}, {4, 34} }); PathData e = Polygon({ {18, 18}, {48, 18}, {48, 44}, {18, 44} });
						d.P.insert(d.P.end(), e.P.begin(), e.P.end()); d.V.insert(d.V.end(), e.V.begin(), e.V.end()); return d; }(), 0) });
		Add("winding overlap non-zero" + A, Pixel, aa, false,
			{ Fill([] { PathData d = Polygon({ {4, 4}, {34, 4}, {34, 34}, {4, 34} }); PathData e = Polygon({ {18, 18}, {48, 18}, {48, 44}, {18, 44} });
						d.P.insert(d.P.end(), e.P.begin(), e.P.end()); d.V.insert(d.V.end(), e.V.begin(), e.V.end()); return d; }(), 1) });
		Add("open path filled as closed" + A, Pixel, aa, false, { Fill(Polygon({ {3, 3}, {60, 6}, {20, 44} }, false), 0) });
		PathData ell; AppendEllipse(ell, 20.3, 22.7, 16.2, 11.9); AppendEllipse(ell, 46, 24, 12, 18);
		Add("ellipses (cubic)" + A, Pixel, aa, false, { Fill(ell, 1) });
		Add("ellipse rotated 30 scaled 0.81" + A, Similarity(0.81, 30, 30, -2), aa, false, { Fill(ell, 1) });
		std::mt19937 rng(100 + aa);
		std::uniform_real_distribution<double> X(-6, W + 6), Y(-6, H + 6);
		for (int k = 0; k < 6; k++)
		{
			std::vector<std::pair<double, double>> pts;
			for (int i = 0; i < 7; i++) pts.push_back({ X(rng), Y(rng) });
			Add("random polygon " + std::to_string(k) + A, Pixel, aa, false, { Fill(Polygon(pts), DWORD(k & 1)) });
		}
	}

	// Strokes.
	const double Widths[] = { 0.5, 1, 1.5, 2, 3, 4.7 };
	const CapStyles Caps[] = { CAPS_BUTT, CAPS_ROUND, CAPS_SQUARE };
	const JoinStyles Joins[] = { JOIN_MITER, JOIN_ROUND, JOIN_BEVEL };
	for (int aa = 0; aa <= 1; aa++)
		for (int hint = 0; hint <= 1; hint++)
		{
			const std::string A = std::string(" aa=") + char('0' + aa) + " hint=" + char('0' + hint);
			for (double w : Widths)
			{
				std::vector<Ops> lines;
				for (int i = 0; i < 5; i++)
				{
					const double y = 4 + i * 8.6 + 0.13 * i;
					lines.push_back(Stroke(Polygon({ {3 + 0.31 * i, y}, {60 - 0.27 * i, y} }, false), w, CAPS_BUTT, JOIN_MITER));
					lines.push_back(Stroke(Polygon({ {3 + i * 11.4, 2}, {3 + i * 11.4, 45} }, false), w, CAPS_BUTT, JOIN_MITER));
				}
				char n[64]; std::snprintf(n, sizeof n, "axis lines w=%.1f", w);
				Add(n + A, Pixel, aa, hint, lines);
			}
			for (int c = 0; c < 3; c++)
				for (int j = 0; j < 3; j++)
				{
					char n[64]; std::snprintf(n, sizeof n, "zigzag cap=%d join=%d w=3.3", c, j);
					Add(n + A, Pixel, aa, hint, { Stroke(Polygon({ {6, 8}, {20, 40}, {30, 12}, {44, 38}, {58, 10} }, false), 3.3, Caps[c], Joins[j]) });
				}
			PathData ell; AppendEllipse(ell, 32, 24, 22.5, 15.25);
			Add("ellipse stroke w=1" + A, Pixel, aa, hint, { Stroke(ell, 1, CAPS_ROUND, JOIN_ROUND) });
			Add("ellipse stroke w=2.6 rotated" + A, Similarity(0.9, 17, 4, -3), aa, hint, { Stroke(ell, 2.6, CAPS_ROUND, JOIN_ROUND) });
			if (aa && hint)
				v.back().Known = "GDraw also hints flattened curve segments that come out exactly vertical/horizontal; its flattening is not observable through the API";
			Add("closed square stroke w=2" + A, Pixel, aa, hint, { Stroke(Polygon({ {8, 8}, {40, 8}, {40, 40}, {8, 40} }), 2, CAPS_BUTT, JOIN_MITER) });
		}
	for (int aa = 0; aa <= 1; aa++)
	{
		const std::string A = std::string(" aa=") + char('0' + aa);
		PathData ell; AppendEllipse(ell, 40, 24, 14, 12);
		Add("reflective colour: rect+point+fill+stroke" + A, Pixel, aa, false,
			{ Reflective(RGB(0, 0, 0)), FillRect(2.5, 2.5, 20.5, 30.5), FillPoint(24.5, 40.5), Fill(ell, 0),
			  Stroke(Polygon({ {3, 44}, {60, 36} }, false), 2.2, CAPS_ROUND, JOIN_ROUND) });
	}
	for (int hint = 0; hint <= 1; hint++)
	{
		std::vector<Ops> thin;
		for (int i = 0; i < 8; i++)
		{
			const double y = 3 + i * 5.5 + (i & 1) * 0.5;
			thin.push_back(Stroke(Polygon({ {4 + i * 0.25, y}, {20 + i * 4.3, y + (i & 2) * 0.5} }, false), 1 + (i & 1) * 0.5, CAPS_ROUND, JOIN_ROUND));
		}
		Add(std::string("thin round-capped lines hint=") + char('0' + hint), Pixel, true, hint, thin);
	}
	Add("miter limit sharp angles", Pixel, true, false,
		{ Stroke(Polygon({ {4, 40}, {20, 4}, {24, 40}, {30, 4}, {31, 40}, {40, 5}, {40.6, 40} }, false), 2, CAPS_BUTT, JOIN_MITER) });

	// Clipping.
	for (int aa = 0; aa <= 1; aa++)
	{
		const std::string A = std::string(" aa=") + char('0' + aa);
		PathData ell; AppendEllipse(ell, 32, 24, 30, 22);
		Add("device clip + ellipse" + A, Pixel, aa, false, { DeviceClip(10, 7, 50, 31), Fill(ell, 0) });
		Add("clip rect + ellipse" + A, Pixel, aa, false, { ClipRect(5.5, 4.25, 41.5, 36.75), Fill(ell, 0) });
		Add("clip nested + stroke + rect" + A, Pixel, aa, false,
			{ ClipRect(3, 3, 50, 40), ClipRect(10, 10, 60, 45), Stroke(ell, 3, CAPS_ROUND, JOIN_ROUND), FillRect(0, 0, 64, 12) });
		Add("clip reset (infinite device clip)" + A, Pixel, aa, false,
			{ ClipRect(10, 10, 20, 20), DeviceClip(INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN), Fill(ell, 0) });
	}
	return v;
}

int main(int argc, char** argv)
{
	FILE* pCsv = NULL;
	std::string Filter;
	for (int i = 1; i < argc; i++)
	{
		if (std::string(argv[i]) == "-v") g_Verbose = true;
		else if (std::string(argv[i]) == "-s" && i + 1 < argc) Filter = argv[++i];
		else pCsv = std::fopen(argv[i], "w");
	}
	if (pCsv)
		std::fprintf(pCsv, "scene,pass,exact_diff,exact,edge_diff,edge,max_edge_diff,area_error,bbox_covers,bbox_slack,top_byte_diff,output_hash\n");
	std::vector<Scene> scenes = BuildScenes();
	if (!Filter.empty())
	{
		std::vector<Scene> Kept;
		for (const Scene& s : scenes)
			if (s.Name.find(Filter) != std::string::npos) Kept.push_back(s);
		scenes.swap(Kept);
	}
	int Failed = 0;
	for (const Scene& s : scenes)
		for (int TopDown = 0; TopDown <= 1; TopDown++)
			for (DWORD Background : { 0x00ffffffu, 0x40ffffffu })
				if (!RunScene(s, TopDown != 0, Background, pCsv)) Failed++;
	size_t Runs = scenes.size() * 4;
	for (const Scene& s : scenes)
		for (int TopDown = 0; TopDown <= 1; TopDown++)
			for (int Kind = 0; Kind < RGBT_KINDS; Kind++, Runs++)
				if (!RunSceneRGBT(s, TopDown != 0, Kind, pCsv)) Failed++;
	if (pCsv) std::fclose(pCsv);
	std::printf("%s: %zu runs, %d failed, %d known differences (AA edge tolerance %d levels, area %.0f%% or bias %.2f, non-AA flips %.0f%% of edge pixels)\n",
				Failed ? "FAIL" : "PASS", Runs, Failed, g_Known, kMaxEdgeDiff, kMaxAreaError * 100, kMaxEdgeBias, kMaxBinaryFlips * 100);
	return Failed ? 1 : 0;
}
