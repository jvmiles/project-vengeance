// SPDX-License-Identifier: GPL-2.0-only
// Cairo rasterisation core for GDrawCairo (Stage 2A).
//
// Independent of Xara headers so it can be differential-tested against CDraw in a
// standalone program. The public structures mirror the layout of the GDraw
// POINT, RECT and GMATRIX types; GDrawCairo static_asserts that equivalence.
//
// Conventions reproduced (measured from CDraw 4.4 through its public API, see
// tests/native/gdraw_conventions_probe.cpp):
//  - Device pixel = (A*x + B*y + C) / 2^30 (FX = 14 plus 16), y up from the bottom row.
//  - Target: 32bpp words, stride Width*4: BI_RGB (0x00RRGGBB) or the GDraw 0x80000001
//    transparency-channel format (top byte T = 255 - alpha, premultiplied colour);
//    bottom-up (GDraw_SetDIBitmap, used by GDrawAsm on wxGTK because USE_wxBITMAP is 0)
//    or top-down (SetInvertedDIBitmap).
//  - Top byte, BI_RGB targets: with a reflective colour every operation keeps it;
//    otherwise non-antialiased operations and FillRectangle/FillPoint write the full
//    word (top byte 0), while antialiased path fills and strokes keep it (partial
//    pixels blend only RGB; 4-pixel groups inside full runs are written whole).
//  - Transparency-channel targets: full coverage writes the colour with T = 0 for every
//    opaque colour call; partial coverage c blends all four channels towards
//    (colour, T = 0): T' = T(1 - c), RGB' = RGB + (C - RGB) c.
//  - Device RECTs are {left, top, right, bottom} with top < bottom in y-up rows and
//    exclusive right/bottom; the empty changed box is {INT_MAX, INT_MAX, INT_MIN, INT_MIN}.
//  - FillRectangle/FillPoint snap to whole pixels (round half up) and never antialias.
//  - Non-antialiased fills sample pixel centres; a centre exactly on a left or
//    bottom edge is outside, on a right or top edge inside.
//  - Hinting snaps stroke points to pixel centres (odd device width) or pixel edges.
//
// Cairo is the rasteriser (flattening, stroking, scan conversion into an A8 coverage
// mask); compositing of the solid colour is done here so the pixel format matches.
#ifndef XARA_CAIRO_CORE_H
#define XARA_CAIRO_CORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct _cairo;
struct _cairo_surface;

namespace XaraCairo
{

struct DevicePoint { std::int32_t x, y; };
struct DeviceRect { std::int32_t left, top, right, bottom; };
struct DeviceMatrix { std::int32_t AX, AY, BX, BY; std::int64_t CX, CY; };

enum { VERB_CLOSEFIGURE = 1, VERB_LINETO = 2, VERB_BEZIERTO = 4, VERB_MOVETO = 6 };
enum CapStyle { CAP_BUTT = 0, CAP_ROUND = 1, CAP_SQUARE = 2 };
enum JoinStyle { JOIN_MITER = 0, JOIN_ROUND = 1, JOIN_BEVEL = 2 };

// Result of a drawing request. UNSUPPORTED means nothing was written and the caller
// must render the operation with the reference (GDraw) renderer instead.
enum Result { DRAWN, UNSUPPORTED };

// Decides whether the current GDraw state can be drawn by Renderer. GDrawCairo mirrors
// every state call into it; when Blocker() returns a reason, the draw call goes to the
// GDraw reference context instead (which always holds the complete state).
// Measured facts it relies on: a colour call (SetColour, SetSolidColour,
// SetTransparency) replaces the whole paint, including gradient/bitmap fills and
// gradient transparency; SetDIBitmap resets the clip rectangle.
class Gate
{
public:
	Gate();

	void Target(bool Supported) { m_bTarget = Supported; }
	void SolidPaint() { m_pPaint = NULL; }
	void OtherPaint(const char* pReason) { m_pPaint = pReason; }
	void RegionClip(bool Active) { m_bRegion = Active; }
	void SeparationTables(bool Active) { m_bSeparation = Active; }
	void ConversionTable(bool Active) { m_bConversion = Active; }
	void TransparencyTable(bool Active) { m_bTranspTable = Active; }
	void MatrixIntFlag(bool Active) { m_bMatrixInt = Active; }
	void MiterLimitSet() { m_bMiterLimit = true; }

	// NULL when Renderer may draw with the current state, otherwise why not.
	const char* Blocker() const;
	bool MiterLimitChanged() const { return m_bMiterLimit; }

	// SetSolidColour writes the exact colour only for these depths (16/8 bpp quantise).
	static bool IsExactSolidDepth(std::uint32_t BPP) { return BPP == 0 || BPP == 24 || BPP == 32; }
	// SetTransparency that is an opaque solid colour: T_NONE (0) or T_REFLECTIVE (1)
	// with a zero transparency byte. T_NONE writes a non-zero byte into the pixel.
	static bool IsOpaqueTransparency(std::uint32_t RGBT, std::uint32_t Style)
	{
		return (Style == 0 || Style == 1) && (RGBT >> 24) == 0;
	}

private:
	bool m_bTarget;
	const char* m_pPaint;
	bool m_bRegion, m_bSeparation, m_bConversion, m_bTranspTable, m_bMatrixInt, m_bMiterLimit;
};

#ifdef XARA_CAIRO_PROFILE
void PrintProfile();	// benchmark builds only
#endif

// One Renderer per drawing context. It keeps reusable scratch state (path coordinates and
// a pooled coverage mask with its cairo context), so an instance must not be used from
// two threads at once; instances share nothing.
class Renderer
{
public:
	Renderer();
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// Target pixels are caller-owned and must outlive their use.
	// TransparencyChannel: GDraw 0x80000001 bitmaps, whose top byte is transparency
	// (T = 255 - alpha) with premultiplied colour.
	bool SetTarget(std::uint32_t* pBits, std::int32_t Width, std::int32_t Height, bool TopDown,
				   bool TransparencyChannel = false);
	bool IsTransparencyTarget() const { return m_bRGBT; }
	void ClearTarget();
	bool HasTarget() const { return m_pBits != NULL; }

	void SetMatrix(const DeviceMatrix& Matrix) { m_Matrix = Matrix; }
	void SetAntialias(bool Antialias) { m_bAntialias = Antialias; }
	void SetHinting(bool Hinting) { m_bHinting = Hinting; }
	// Curve flattening tolerance in document units (GDraw_SetFlatness).
	void SetFlatness(std::uint32_t Flatness) { m_Flatness = Flatness; }
	// 0x00BBGGRR. Reflective: the colour came from SetTransparency(T_REFLECTIVE, 0), which
	// makes GDraw keep the destination's top byte for every operation.
	void SetColour(std::uint32_t ColourRef, bool Reflective = false);
	void SetDeviceClip(const DeviceRect& Clip) { m_Clip = Clip; }

	void ClearChangedBBox();
	DeviceRect GetChangedBBox() const { return m_Changed; }
	static DeviceRect EmptyRect();
	static DeviceRect UnionRect(const DeviceRect& A, const DeviceRect& B);

	// DocRect uses document coordinates with GDraw RECT naming (top = larger y).
	Result FillRectangle(const DeviceRect& DocRect);
	Result FillPoint(const DevicePoint& DocPoint);
	// Winding: 0 even-odd, 1 non-zero. Inverse windings (2, 3) are unsupported.
	Result FillPath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
					std::uint32_t Winding);
	// Close must be false (per-subpath PT_CLOSEFIGURE flags are honoured); LineWidth is
	// in document units. Dashes are not supported by this core.
	Result StrokePath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
					  bool Close, std::uint32_t LineWidth, CapStyle Cap, JoinStyle Join);

	// Why the last request returned UNSUPPORTED (static string).
	const char* LastUnsupportedReason() const { return m_pReason; }

	// Shared validation used by the adapter before choosing a renderer.
	static bool IsValidPath(const std::uint8_t* pVerbs, std::uint32_t Count);
	static bool IsSimilarity(const DeviceMatrix& Matrix);

private:
	Result Unsupported(const char* pReason) { m_pReason = pReason; return UNSUPPORTED; }
	// Device coordinates (memory rows) of a valid path into m_PathX/m_PathY, with stroke
	// hinting applied; returns their bounding box.
	void PreparePath(const DevicePoint* pPoints, const std::uint8_t* pVerbs, std::uint32_t Count,
					 bool Hint, bool OddWidth, double& MinX, double& MinY, double& MaxX, double& MaxY);
	void EmitPath(const std::uint8_t* pVerbs, std::uint32_t Count);
	bool EnsureMask(int Width, int Height);
	void ReleaseMask();
	std::int32_t SnapRect(__int128 Raw) const;
	bool ClipBox(double x0, double y0, double x1, double y1, DeviceRect& Box) const;
	void CompositeMask(const unsigned char* pMask, int Stride, const DeviceRect& Box);
	void FillSpan(const DeviceRect& Box);
	void AddChanged(const DeviceRect& MemoryBox);
	// Device rows (y up) <-> memory rows (cairo y); the mapping is its own inverse.
	void MapRows(double Row0, double Row1, double& Out0, double& Out1) const
	{
		if (m_bTopDown) { Out0 = m_Height - Row1; Out1 = m_Height - Row0; }
		else { Out0 = Row0; Out1 = Row1; }
	}
	double MapY(double DeviceY) const { return m_bTopDown ? m_Height - DeviceY : DeviceY; }
	Result RasteriseAndComposite(const std::uint8_t* pVerbs, std::uint32_t Count, double MinX, double MinY,
								 double MaxX, double MaxY, bool Stroke, bool EvenOdd, double LineWidth,
								 CapStyle Cap, JoinStyle Join);

	std::uint32_t* m_pBits;
	std::int32_t m_Width, m_Height;
	bool m_bTopDown;			// memory row 0 is the top row (GDraw "inverted" DIB)
	bool m_bRGBT;				// 0x80000001 target: top byte is transparency
	DeviceMatrix m_Matrix;
	bool m_bAntialias;
	bool m_bHinting;
	std::uint32_t m_Flatness;	// document units; 0 = cairo default
	std::uint32_t m_Colour;		// 0x00RRGGBB pixel value
	bool m_bReflective;			// keep the destination top byte for all operations
	DeviceRect m_Clip;			// device pixels, y up
	DeviceRect m_Changed;		// device pixels, y up
	const char* m_pReason;

	// Reusable scratch (capacity grows, never shared between instances).
	std::vector<__int128> m_RawX, m_RawY;
	std::vector<char> m_SnapX, m_SnapY;
	std::vector<double> m_PathX, m_PathY;
	_cairo_surface* m_pMask;	// A8 coverage mask, all zero between draw calls
	_cairo* m_pMaskCr;
	int m_MaskW, m_MaskH;
};

}

#endif
