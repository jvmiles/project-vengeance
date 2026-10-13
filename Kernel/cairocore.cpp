// SPDX-License-Identifier: GPL-2.0-only
// Cairo rasterisation core for GDrawCairo (Stage 2A). See cairocore.h for conventions.

#include "cairocore.h"

#include <cairo.h>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

// Optional phase profiling (benchmark builds with -DXARA_CAIRO_PROFILE only; the globals
// below exist only in that configuration, which is single-threaded by construction).
#ifdef XARA_CAIRO_PROFILE
#include <chrono>
#include <cstdio>
namespace
{
enum { PH_BUILD, PH_EXTENTS, PH_MASK, PH_RASTER, PH_COMPOSITE, PH_CLEANUP, PH_COUNT };
const char* const kPhaseNames[] = { "build path", "extents", "mask setup", "rasterise", "composite", "cleanup" };
double g_PhaseMs[PH_COUNT];
std::chrono::steady_clock::time_point g_Last;
void ProfBegin() { g_Last = std::chrono::steady_clock::now(); }
void ProfMark(int Phase)
{
	const auto Now = std::chrono::steady_clock::now();
	g_PhaseMs[Phase] += std::chrono::duration<double, std::milli>(Now - g_Last).count();
	g_Last = Now;
}
}
namespace XaraCairo
{
void PrintProfile()
{
	double Total = 0;
	for (double t : g_PhaseMs) Total += t;
	std::printf("core phase profile (all cells, ms and share):\n");
	for (int i = 0; i < PH_COUNT; i++)
		std::printf("  %-12s %9.1f ms  %5.1f%%\n", kPhaseNames[i], g_PhaseMs[i], Total > 0 ? 100 * g_PhaseMs[i] / Total : 0);
}
}
#define PROF_BEGIN() ProfBegin()
#define PROF_MARK(Phase) ProfMark(Phase)
#else
#define PROF_BEGIN() ((void)0)
#define PROF_MARK(Phase) ((void)0)
#endif

namespace XaraCairo
{

static const double kDeviceOne = 1073741824.0;		// 2^30: one pixel in GMATRIX output units
// Shifts non-AA x ties to GDraw's rule (left edge exclusive, right inclusive). Cairo
// stores coordinates as 24.8 fixed point, so this is the smallest shift that survives.
static const double kTieEpsilon = 1.0 / 256.0;

Gate::Gate()
	: m_bTarget(false), m_pPaint("paint before the first colour call"), m_bRegion(false),
	  m_bSeparation(false), m_bConversion(false), m_bTranspTable(false), m_bMatrixInt(false),
	  m_bMiterLimit(false)
{
}

const char* Gate::Blocker() const
{
	if (!m_bTarget)
		return "bitmap formats other than 32bpp BI_RGB or transparency-channel";
	if (m_pPaint)
		return m_pPaint;
	if (m_bRegion)
		return "clip regions";
	if (m_bSeparation)
		return "colour separation tables";
	if (m_bConversion)
		return "bitmap conversion tables";
	if (m_bTranspTable)
		return "transparency lookup tables";
	if (m_bMatrixInt)
		return "integer matrix mode";
	return NULL;
}

Renderer::Renderer()
	: m_pBits(NULL), m_Width(0), m_Height(0), m_bTopDown(false), m_bRGBT(false), m_bAntialias(true), m_bHinting(false), m_Flatness(0),
	  m_Colour(0), m_bReflective(false), m_pReason(""), m_pMask(NULL), m_pMaskCr(NULL), m_MaskW(0), m_MaskH(0)
{
	DeviceMatrix Identity = { 1 << 30, 0, 0, 1 << 30, 0, 0 };
	m_Matrix = Identity;
	m_Clip = EmptyRect();
	m_Changed = EmptyRect();
}

Renderer::~Renderer()
{
	ReleaseMask();
}

void Renderer::ReleaseMask()
{
	if (m_pMaskCr)
		cairo_destroy(m_pMaskCr);
	if (m_pMask)
		cairo_surface_destroy(m_pMask);
	m_pMaskCr = NULL;
	m_pMask = NULL;
	m_MaskW = m_MaskH = 0;
}

// Grow-only pooled mask; a fresh image surface is zero-filled by cairo.
bool Renderer::EnsureMask(int Width, int Height)
{
	if (m_pMaskCr && Width <= m_MaskW && Height <= m_MaskH)
		return true;
	const int NewW = Width > m_MaskW ? Width : m_MaskW, NewH = Height > m_MaskH ? Height : m_MaskH;
	ReleaseMask();
	m_pMask = cairo_image_surface_create(CAIRO_FORMAT_A8, NewW, NewH);
	if (cairo_surface_status(m_pMask) != CAIRO_STATUS_SUCCESS)
	{
		ReleaseMask();
		return false;
	}
	m_pMaskCr = cairo_create(m_pMask);
	if (cairo_status(m_pMaskCr) != CAIRO_STATUS_SUCCESS)
	{
		ReleaseMask();
		return false;
	}
	cairo_set_source_rgba(m_pMaskCr, 0, 0, 0, 1);
	m_MaskW = NewW;
	m_MaskH = NewH;
	return true;
}

bool Renderer::SetTarget(std::uint32_t* pBits, std::int32_t Width, std::int32_t Height, bool TopDown,
						 bool TransparencyChannel)
{
	ClearTarget();
	if (pBits == NULL || Width <= 0 || Height <= 0 ||
		Width > INT32_MAX / 4 || cairo_format_stride_for_width(CAIRO_FORMAT_A8, Width) < 0)
		return false;
	m_pBits = pBits;
	m_Width = Width;
	m_Height = Height;
	m_bTopDown = TopDown;
	m_bRGBT = TransparencyChannel;
	DeviceRect Full = { 0, 0, Width, Height };
	m_Clip = Full;
	return true;
}

void Renderer::ClearTarget()
{
	m_pBits = NULL;
	m_Width = m_Height = 0;
	m_bRGBT = false;
	m_Clip = EmptyRect();
}

void Renderer::SetColour(std::uint32_t ColourRef, bool Reflective)
{
	// COLORREF 0x00BBGGRR -> pixel 0x00RRGGBB
	m_Colour = ((ColourRef & 0xff) << 16) | (ColourRef & 0xff00) | ((ColourRef >> 16) & 0xff);
	m_bReflective = Reflective;
}

DeviceRect Renderer::EmptyRect()
{
	DeviceRect r = { INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
	return r;
}

DeviceRect Renderer::UnionRect(const DeviceRect& A, const DeviceRect& B)
{
	DeviceRect r;
	r.left = A.left < B.left ? A.left : B.left;
	r.top = A.top < B.top ? A.top : B.top;
	r.right = A.right > B.right ? A.right : B.right;
	r.bottom = A.bottom > B.bottom ? A.bottom : B.bottom;
	return r;
}

void Renderer::ClearChangedBBox()
{
	m_Changed = EmptyRect();
}

bool Renderer::IsSimilarity(const DeviceMatrix& M)
{
	return (M.AX == M.BY && M.AY == -M.BX) || (M.AX == -M.BY && M.AY == M.BX);
}

bool Renderer::IsValidPath(const std::uint8_t* pVerbs, std::uint32_t Count)
{
	if (pVerbs == NULL || Count == 0 || pVerbs[0] != VERB_MOVETO)
		return false;
	for (std::uint32_t i = 0; i < Count; i++)
	{
		const std::uint8_t Verb = pVerbs[i] & ~VERB_CLOSEFIGURE;
		const bool Close = (pVerbs[i] & VERB_CLOSEFIGURE) != 0;
		if (Verb == VERB_MOVETO)
		{
			if (Close)
				return false;
		}
		else if (Verb == VERB_BEZIERTO)
		{
			if (i + 2 >= Count || Close ||
				pVerbs[i + 1] != VERB_BEZIERTO ||
				(pVerbs[i + 2] & ~VERB_CLOSEFIGURE) != VERB_BEZIERTO)
				return false;
			i += 2;
		}
		else if (Verb != VERB_LINETO)
			return false;
	}
	return true;
}

std::int32_t Renderer::SnapRect(__int128 Raw) const
{
	// floor(Raw / 2^30 + 0.5), clamped to the 32-bit pixel range
	const __int128 v = (Raw + (__int128(1) << 29)) >> 30;
	if (v > INT32_MAX) return INT32_MAX;
	if (v < INT32_MIN) return INT32_MIN;
	return std::int32_t(v);
}

// Intersect an area in memory rows (cairo coordinates) with the target and device clip.
bool Renderer::ClipBox(double x0, double y0, double x1, double y1, DeviceRect& Box) const
{
	double l = std::floor(x0), t = std::floor(y0), r = std::ceil(x1), b = std::ceil(y1);
	const double cl = m_Clip.left, cr = m_Clip.right;
	double ct, cb;
	MapRows(m_Clip.top, m_Clip.bottom, ct, cb);
	if (l < cl) l = cl;
	if (r > cr) r = cr;
	if (t < ct) t = ct;
	if (b > cb) b = cb;
	if (l < 0) l = 0;
	if (t < 0) t = 0;
	if (r > m_Width) r = m_Width;
	if (b > m_Height) b = m_Height;
	if (l >= r || t >= b)
		return false;
	Box.left = std::int32_t(l);
	Box.top = std::int32_t(t);
	Box.right = std::int32_t(r);
	Box.bottom = std::int32_t(b);
	return true;
}

void Renderer::AddChanged(const DeviceRect& MemoryBox)
{
	double t, b;
	MapRows(MemoryBox.top, MemoryBox.bottom, t, b);
	DeviceRect Device = { MemoryBox.left, std::int32_t(t), MemoryBox.right, std::int32_t(b) };
	m_Changed = UnionRect(m_Changed, Device);
}

void Renderer::FillSpan(const DeviceRect& Box)
{
	for (std::int32_t y = Box.top; y < Box.bottom; y++)
	{
		std::uint32_t* pRow = m_pBits + std::size_t(y) * m_Width;
		for (std::int32_t x = Box.left; x < Box.right; x++)
			pRow[x] = (m_bReflective && !m_bRGBT) ? (pRow[x] & 0xff000000) | m_Colour : m_Colour;
	}
	AddChanged(Box);
}

static inline std::uint32_t BlendChannel(std::uint32_t Dst, std::uint32_t Src, std::uint32_t Cov)
{
	const int Delta = int(Src) - int(Dst);
	const int Step = (Delta * int(Cov) + (Delta >= 0 ? 127 : -127)) / 255;
	return std::uint32_t(int(Dst) + Step);
}

static inline std::uint64_t Load8(const unsigned char* p)
{
	std::uint64_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

void Renderer::CompositeMask(const unsigned char* pMask, int Stride, const DeviceRect& Box)
{
	DeviceRect Touched = { INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
	const std::uint32_t SR = (m_Colour >> 16) & 0xff, SG = (m_Colour >> 8) & 0xff, SB = m_Colour & 0xff;
	// Top byte, as measured in GDraw: reflective colours keep it everywhere; non-AA writes
	// the full word. AA keeps it for partial pixels and for fully covered pixels at run
	// ends, but writes the full word for each 4-pixel group [4k, 4k+4) lying entirely
	// inside a run of fully covered pixels (its fast span path).
	// Transparency-channel targets (0x80000001): every operation writes T = 0 on full
	// coverage and blends all four channels towards (colour, T = 0) on partial coverage,
	// i.e. premultiplied "over" with inverted alpha, for any opaque colour call.
	const bool KeepTop = !m_bRGBT && (m_bAntialias || m_bReflective);
	const bool GroupWrites = !m_bRGBT && m_bAntialias && !m_bReflective;
	const std::uint32_t Colour = m_Colour;
	for (std::int32_t y = Box.top; y < Box.bottom; y++)
	{
		// Index the mask row by x so runs can be scanned without offsets.
		const unsigned char* pCov = pMask + std::size_t(y - Box.top) * Stride - Box.left;
		std::uint32_t* pRow = m_pBits + std::size_t(y) * m_Width;
		std::int32_t RowFirst = INT32_MAX, RowEnd = INT32_MIN;
		std::int32_t x = Box.left;
		const std::int32_t End = Box.right;
		while (x < End)
		{
			// Skip uncovered pixels, eight mask bytes at a time.
			while (x + 8 <= End && Load8(pCov + x) == 0)
				x += 8;
			while (x < End && pCov[x] == 0)
				x++;
			if (x >= End)
				break;
			if (x < RowFirst)
				RowFirst = x;
			if (pCov[x] == 255)
			{
				// Maximal run of full coverage [x, RunEnd).
				std::int32_t RunEnd = x + 1;
				while (RunEnd + 8 <= End && Load8(pCov + RunEnd) == ~std::uint64_t(0))
					RunEnd += 8;
				while (RunEnd < End && pCov[RunEnd] == 255)
					RunEnd++;
				// Whole words are written for [FullFrom, FullTo); the rest keep the top byte.
				std::int32_t FullFrom = x, FullTo = RunEnd;
				if (KeepTop)
				{
					FullFrom = FullTo = x;
					if (GroupWrites)
					{
						const std::int32_t GroupFrom = (x + 3) & ~3, GroupTo = RunEnd & ~3;
						if (GroupFrom < GroupTo)
						{
							FullFrom = GroupFrom;
							FullTo = GroupTo;
						}
					}
				}
				for (std::int32_t i = x; i < FullFrom; i++)
					pRow[i] = (pRow[i] & 0xff000000) | Colour;
				for (std::int32_t i = FullFrom; i < FullTo; i++)
					pRow[i] = Colour;
				for (std::int32_t i = FullTo; i < RunEnd; i++)
					pRow[i] = (pRow[i] & 0xff000000) | Colour;
				RowEnd = RunEnd;
				x = RunEnd;
			}
			else
			{
				const std::uint32_t Cov = pCov[x];
				const std::uint32_t D = pRow[x];
				const std::uint32_t T = KeepTop ? D >> 24 : BlendChannel(D >> 24, 0, Cov);
				const std::uint32_t R = BlendChannel((D >> 16) & 0xff, SR, Cov);
				const std::uint32_t G = BlendChannel((D >> 8) & 0xff, SG, Cov);
				const std::uint32_t B = BlendChannel(D & 0xff, SB, Cov);
				pRow[x] = (T << 24) | (R << 16) | (G << 8) | B;
				RowEnd = ++x;
			}
		}
		if (RowFirst < RowEnd)
		{
			if (RowFirst < Touched.left) Touched.left = RowFirst;
			if (RowEnd > Touched.right) Touched.right = RowEnd;
			if (y < Touched.top) Touched.top = y;
			if (y + 1 > Touched.bottom) Touched.bottom = y + 1;
		}
	}
	if (Touched.left < Touched.right)
		AddChanged(Touched);
}

// Rasterise the prepared path (m_PathX/m_PathY) into the pooled mask and composite it.
// The path is emitted once, into a mask region chosen from a conservative bound; exact
// cairo extents then decide the composited box and the changed box, exactly as before.
Result Renderer::RasteriseAndComposite(const std::uint8_t* pVerbs, std::uint32_t Count, double MinX, double MinY,
									   double MaxX, double MaxY, bool Stroke, bool EvenOdd, double LineWidth,
									   CapStyle Cap, JoinStyle Join)
{
	// Non-AA ties: GDraw excludes centres on left/bottom edges and includes them on
	// right/top edges; pixman includes the left and lowest-memory-row edges.
	const double ShiftX = m_bAntialias ? 0.0 : kTieEpsilon;
	const double ShiftY = (m_bAntialias || m_bTopDown) ? 0.0 : kTieEpsilon;
	const cairo_fill_rule_t Rule = EvenOdd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING;
	const cairo_line_cap_t Caps[] = { CAIRO_LINE_CAP_BUTT, CAIRO_LINE_CAP_ROUND, CAIRO_LINE_CAP_SQUARE };
	const cairo_line_join_t Joins[] = { CAIRO_LINE_JOIN_MITER, CAIRO_LINE_JOIN_ROUND, CAIRO_LINE_JOIN_BEVEL };
	// GDraw flattens curves to within m_Flatness document units; cairo's tolerance is
	// the same quantity in device pixels.
	const double Det = std::fabs(double(m_Matrix.AX) * m_Matrix.BY - double(m_Matrix.AY) * m_Matrix.BX);
	double Tolerance = m_Flatness ? m_Flatness * std::sqrt(Det) / kDeviceOne : 0.1;
	if (Tolerance < 0.01)
		Tolerance = 0.01;

	// Conservative mask region: control points (curves lie in their hull) padded for the
	// stroke (miter joins reach at most limit 10 x half width; square caps < width).
	const double Pad = (Stroke ? LineWidth * (Join == JOIN_MITER ? 5.0 : 1.0) : 0.0) + 2.0;
	DeviceRect Region;
	if (!ClipBox(MinX + ShiftX - Pad, MinY + ShiftY - Pad, MaxX + ShiftX + Pad, MaxY + ShiftY + Pad, Region))
		return DRAWN;		// entirely clipped: nothing to change
	for (int Attempt = 0; Attempt < 2; Attempt++)
	{
		if (!EnsureMask(Region.right - Region.left, Region.bottom - Region.top))
			return Unsupported("cairo could not allocate a coverage mask");
		cairo_t* pCr = m_pMaskCr;
		cairo_new_path(pCr);
		cairo_reset_clip(pCr);
		cairo_identity_matrix(pCr);
		// Only the region may be written, so the rest of the pooled mask stays zero.
		cairo_rectangle(pCr, 0, 0, Region.right - Region.left, Region.bottom - Region.top);
		cairo_clip(pCr);
		cairo_translate(pCr, ShiftX - Region.left, ShiftY - Region.top);
		// Extents are measured with the default antialias mode (cairo rounds them
		// differently under ANTIALIAS_NONE); the real mode is set before rasterising.
		cairo_set_antialias(pCr, CAIRO_ANTIALIAS_DEFAULT);
		cairo_set_tolerance(pCr, Tolerance);
		cairo_set_fill_rule(pCr, Rule);
		cairo_set_line_width(pCr, LineWidth);
		cairo_set_line_cap(pCr, Caps[Cap]);
		cairo_set_line_join(pCr, Joins[Join]);
		cairo_set_miter_limit(pCr, 10.0);
		EmitPath(pVerbs, Count);
		PROF_MARK(PH_BUILD);

		// Exact geometric extents (user space = memory rows) decide what is composited.
		double x0, y0, x1, y1;
		if (Stroke)
			cairo_stroke_extents(pCr, &x0, &y0, &x1, &y1);
		else
			cairo_fill_extents(pCr, &x0, &y0, &x1, &y1);
		PROF_MARK(PH_EXTENTS);
		if (cairo_status(pCr) != CAIRO_STATUS_SUCCESS)
		{
			ReleaseMask();
			return Unsupported("cairo could not measure the path");
		}
		DeviceRect Box;
		if (!ClipBox(x0 - 1, y0 - 1, x1 + 1, y1 + 1, Box))
		{
			cairo_new_path(pCr);
			return DRAWN;	// entirely clipped or empty: nothing to change
		}
		if (Box.left < Region.left || Box.top < Region.top || Box.right > Region.right || Box.bottom > Region.bottom)
		{
			// The bound was not conservative (should not happen): redo with the exact box.
			Region = Box;
			continue;
		}
		PROF_MARK(PH_MASK);

		cairo_set_antialias(pCr, m_bAntialias ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE);
		if (Stroke)
			cairo_stroke(pCr);
		else
			cairo_fill(pCr);
		const cairo_status_t DrawStatus = cairo_status(pCr);
		cairo_surface_flush(m_pMask);
		PROF_MARK(PH_RASTER);
		unsigned char* pData = cairo_image_surface_get_data(m_pMask);
		const int Stride = cairo_image_surface_get_stride(m_pMask);
		if (DrawStatus == CAIRO_STATUS_SUCCESS)
		{
			CompositeMask(pData + std::size_t(Box.top - Region.top) * Stride + (Box.left - Region.left), Stride, Box);
			// GDraw reports the clipped geometric extents, which may exceed the touched pixels
			// (e.g. thin spikes that sample no pixel centre).
			DeviceRect Geometric;
			if (ClipBox(x0, y0, x1, y1, Geometric))
				AddChanged(Geometric);
		}
		PROF_MARK(PH_COMPOSITE);
		if (DrawStatus != CAIRO_STATUS_SUCCESS)
		{
			ReleaseMask();
			return Unsupported("cairo failed to rasterise the path");
		}
		// Restore the all-zero invariant for the region this call could write.
		const int RegionW = Region.right - Region.left;
		for (int y = 0; y < Region.bottom - Region.top; y++)
			std::memset(pData + std::size_t(y) * Stride, 0, std::size_t(RegionW));
		cairo_surface_mark_dirty_rectangle(m_pMask, 0, 0, RegionW, Region.bottom - Region.top);
		PROF_MARK(PH_CLEANUP);
		return DRAWN;
	}
	ReleaseMask();
	return Unsupported("cairo could not bound the path");
}

Result Renderer::FillRectangle(const DeviceRect& DocRect)
{
	if (!HasTarget())
		return Unsupported("no supported target bitmap");
	if (m_Matrix.AY != 0 || m_Matrix.BX != 0)
		return Unsupported("rectangle under a rotating matrix");
	std::int32_t x0 = SnapRect(__int128(m_Matrix.AX) * DocRect.left + m_Matrix.CX);
	std::int32_t x1 = SnapRect(__int128(m_Matrix.AX) * DocRect.right + m_Matrix.CX);
	std::int32_t y0 = SnapRect(__int128(m_Matrix.BY) * DocRect.bottom + m_Matrix.CY);
	std::int32_t y1 = SnapRect(__int128(m_Matrix.BY) * DocRect.top + m_Matrix.CY);
	if (x0 > x1) { std::int32_t t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { std::int32_t t = y0; y0 = y1; y1 = t; }
	// device rows [y0, y1) (y up) -> memory rows
	double m0, m1;
	MapRows(y0, y1, m0, m1);
	DeviceRect Box;
	if (ClipBox(x0, m0, x1, m1, Box))
		FillSpan(Box);
	return DRAWN;
}

Result Renderer::FillPoint(const DevicePoint& DocPoint)
{
	if (!HasTarget())
		return Unsupported("no supported target bitmap");
	const std::int32_t px = SnapRect(__int128(m_Matrix.AX) * DocPoint.x + __int128(m_Matrix.BX) * DocPoint.y + m_Matrix.CX);
	const std::int32_t py = SnapRect(__int128(m_Matrix.AY) * DocPoint.x + __int128(m_Matrix.BY) * DocPoint.y + m_Matrix.CY);
	DeviceRect Box;
	double m0, m1;
	MapRows(py, double(py) + 1, m0, m1);
	if (ClipBox(px, m0, double(px) + 1, m1, Box))
		FillSpan(Box);
	return DRAWN;
}

// Device coordinates for a valid path (memory rows), into m_PathX/m_PathY.
//
// Hint reproduces GDraw stroke hinting, measured through its API: only horizontal and
// vertical legs (straight segments and the legs of a curve's control polygon) are
// affected, and only their perpendicular coordinate moves, in y-up device space: to a
// pixel centre for an odd rounded device width (floor(v) + 0.5), otherwise to a pixel
// edge (floor(v + 0.5)). Endpoints along a leg and diagonal legs are not changed.
void Renderer::PreparePath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
						   bool Hint, bool OddWidth, double& MinX, double& MinY, double& MaxX, double& MaxY)
{
	// Exact device coordinates in GMATRIX output units (2^-30 pixel).
	m_RawX.resize(Count);
	m_RawY.resize(Count);
	for (std::uint32_t i = 0; i < Count; i++)
	{
		m_RawX[i] = __int128(m_Matrix.AX) * pPoints[i].x + __int128(m_Matrix.BX) * pPoints[i].y + m_Matrix.CX;
		m_RawY[i] = __int128(m_Matrix.AY) * pPoints[i].x + __int128(m_Matrix.BY) * pPoints[i].y + m_Matrix.CY;
	}

	m_SnapX.assign(Count, 0);
	m_SnapY.assign(Count, 0);
	if (Hint)
	{
		std::uint32_t Current = 0, Start = 0;
		auto Segment = [&](std::uint32_t a, std::uint32_t b)
		{
			if (m_RawY[a] == m_RawY[b]) m_SnapY[a] = m_SnapY[b] = 1;
			if (m_RawX[a] == m_RawX[b]) m_SnapX[a] = m_SnapX[b] = 1;
		};
		for (std::uint32_t i = 0; i < Count; i++)
		{
			const std::uint8_t Verb = pVerbs[i] & ~VERB_CLOSEFIGURE;
			if (Verb == VERB_MOVETO)
				Current = Start = i;
			else if (Verb == VERB_LINETO)
			{
				Segment(Current, i);
				Current = i;
			}
			else
			{
				// Curves: the control polygon's horizontal/vertical legs are hinted
				// (measured: e.g. anchors at ellipse extremes move with their controls).
				Segment(Current, i);
				Segment(i, i + 1);
				Segment(i + 1, i + 2);
				i += 2;
				Current = i;
			}
			if (pVerbs[i] & VERB_CLOSEFIGURE)
				Segment(Current, Start);
		}
	}

	m_PathX.resize(Count);
	m_PathY.resize(Count);
	MinX = MinY = 1e300;
	MaxX = MaxY = -1e300;
	for (std::uint32_t i = 0; i < Count; i++)
	{
		double dx = double(m_RawX[i]) / kDeviceOne, dy = double(m_RawY[i]) / kDeviceOne;
		if (m_SnapX[i]) dx = OddWidth ? std::floor(dx) + 0.5 : std::floor(dx + 0.5);
		if (m_SnapY[i]) dy = OddWidth ? std::floor(dy) + 0.5 : std::floor(dy + 0.5);
		// Quantise to cairo's 24.8 fixed point first (round half to even, as cairo's own
		// conversion), so the non-AA tie shift added later through the CTM is applied to
		// already-quantised coordinates; otherwise half-step values round differently.
		dx = std::nearbyint(dx * 256.0) / 256.0;
		const double cy = std::nearbyint(MapY(dy) * 256.0) / 256.0;
		m_PathX[i] = dx;
		m_PathY[i] = cy;
		if (dx < MinX) MinX = dx;
		if (dx > MaxX) MaxX = dx;
		if (cy < MinY) MinY = cy;
		if (cy > MaxY) MaxY = cy;
	}
}

void Renderer::EmitPath(const std::uint8_t* pVerbs, std::uint32_t Count)
{
	cairo_t* pCr = m_pMaskCr;
	for (std::uint32_t i = 0; i < Count; i++)
	{
		const std::uint8_t Verb = pVerbs[i] & ~VERB_CLOSEFIGURE;
		if (Verb == VERB_MOVETO)
			cairo_move_to(pCr, m_PathX[i], m_PathY[i]);
		else if (Verb == VERB_LINETO)
			cairo_line_to(pCr, m_PathX[i], m_PathY[i]);
		else
		{
			cairo_curve_to(pCr, m_PathX[i], m_PathY[i], m_PathX[i + 1], m_PathY[i + 1], m_PathX[i + 2], m_PathY[i + 2]);
			i += 2;
		}
		if (pVerbs[i] & VERB_CLOSEFIGURE)
			cairo_close_path(pCr);
	}
}

Result Renderer::FillPath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
						  std::uint32_t Winding)
{
	if (!HasTarget())
		return Unsupported("no supported target bitmap");
	if (Winding > 1)
		return Unsupported("inverse winding fill");
	if (pPoints == NULL || !IsValidPath(pVerbs, Count))
		return Unsupported("path structure not recognised");
	PROF_BEGIN();
	double MinX, MinY, MaxX, MaxY;
	PreparePath(pPoints, pVerbs, Count, false, false, MinX, MinY, MaxX, MaxY);
	return RasteriseAndComposite(pVerbs, Count, MinX, MinY, MaxX, MaxY, false, Winding == 0, 0, CAP_BUTT, JOIN_MITER);
}

Result Renderer::StrokePath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
							bool Close, std::uint32_t LineWidth, CapStyle Cap, JoinStyle Join)
{
	if (!HasTarget())
		return Unsupported("no supported target bitmap");
	if (Close)
		return Unsupported("stroke with forced closure");
	if (!IsSimilarity(m_Matrix))
		return Unsupported("stroke under a non-uniform matrix");
	if (Cap > CAP_SQUARE || Join > JOIN_BEVEL)
		return Unsupported("unknown cap or join style");
	if (pPoints == NULL || !IsValidPath(pVerbs, Count))
		return Unsupported("path structure not recognised");
	if (LineWidth == 0)
		return DRAWN;		// GDraw draws nothing for zero width

	const double Scale = std::sqrt(double(m_Matrix.AX) * m_Matrix.AX + double(m_Matrix.AY) * m_Matrix.AY) / kDeviceOne;
	const double DeviceWidth = LineWidth * Scale;
	const bool OddWidth = (std::int64_t(std::floor(DeviceWidth + 0.5)) & 1) != 0;
	PROF_BEGIN();
	double MinX, MinY, MaxX, MaxY;
	PreparePath(pPoints, pVerbs, Count, m_bHinting, OddWidth, MinX, MinY, MaxX, MaxY);
	return RasteriseAndComposite(pVerbs, Count, MinX, MinY, MaxX, MaxY, true, false, DeviceWidth, Cap, Join);
}

}
