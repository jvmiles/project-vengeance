// SPDX-License-Identifier: GPL-2.0-only
// GDrawContext backed by Cairo for a verified subset, GDraw for everything else.

#include "camtypes.h"
// camconfig.h must be included immediately after camtypes.h
#include "camconfig.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "gdrawcairo.h"
#include "GDrawIntf.h"
#include "grndrgn.h"

static_assert(sizeof(XaraCairo::DevicePoint) == sizeof(POINT), "POINT layout");
static_assert(sizeof(XaraCairo::DeviceRect) == sizeof(RECT), "RECT layout");
static_assert(sizeof(XaraCairo::DeviceMatrix) == sizeof(GMATRIX), "GMATRIX layout");

// GDrawAsm binds DIBs bottom-up unless it uses SetInvertedDIBitmap (see GDrawAsm::SetBitmap).
#if defined(__WXMSW__) || !USE_wxBITMAP
static const bool kTopDownDIB = false;
#else
static const bool kTopDownDIB = true;
#endif

// ---- diagnostics and statistics ----------------------------------------------------------

static const char* const kOpNames[] = { "FillPath", "StrokePath", "FillRectangle", "FillPoint",
										"FillRegion", "PlotBitmap" };

static unsigned long s_Counts[6][2];		// [op][0 = cairo, 1 = gdraw]
static double s_Micros[6][2];				// cumulative wall time of those calls

// XARA_CAIRO_STATS=<file>: per-operation count and cumulative time (microseconds) of who
// drew what, rewritten after every draw call so the file is current even if the process
// is killed (tests and in-editor benchmarks only; GUI thread).
static const char* StatsPath()
{
	static const char* const pPath = getenv("XARA_CAIRO_STATS");
	return (pPath != NULL && pPath[0] != '\0') ? pPath : NULL;
}

static void WriteStats()
{
	const char* pPath = StatsPath();
	if (pPath == NULL)
		return;
	FILE* f = fopen(pPath, "w");
	if (f == NULL)
		return;
	fprintf(f, "operation cairo gdraw cairo_us gdraw_us\n");
	for (int i = 0; i < 6; i++)
		fprintf(f, "%s %lu %lu %.1f %.1f\n", kOpNames[i], s_Counts[i][0], s_Counts[i][1], s_Micros[i][0], s_Micros[i][1]);
	fclose(f);
}

// Times one draw call; reads the clock only when statistics are enabled.
GDrawCairo::StatTimer::StatTimer()
	: m_Start(StatsPath() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point())
{
}

double GDrawCairo::StatTimer::Micros() const
{
	if (StatsPath() == NULL)
		return 0;
	return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - m_Start).count();
}

static void Count(int Op, int Who, double Micros)
{
	s_Counts[Op][Who]++;
	s_Micros[Op][Who] += Micros;
	WriteStats();
}

BOOL GDrawCairo::Drawn(DrawOp Op, const StatTimer& Timer)
{
	Count(Op, 0, Timer.Micros());
	return TRUE;
}

// Each distinct (operation, reason) is reported once per process.
// Called after GDraw has drawn the call, so the timer covers the GDraw draw.
void GDrawCairo::Delegated(DrawOp Op, const char* pReason, const StatTimer& Timer)
{
	static std::set<std::string> s_Reported;
	Count(Op, 1, Timer.Micros());
	std::string Key = std::string(kOpNames[Op]) + ": " + (pReason ? pReason : "unsupported");
	if (s_Reported.insert(Key).second)
		fprintf(stderr, "Xara renderer: cairo does not draw %s with %s in this stage; "
						"drawing it with gdraw.\n", kOpNames[Op], pReason ? pReason : "this state");
}

// ---- lifetime --------------------------------------------------------------------------

GDrawCairo::GDrawCairo() : GDrawContext(), m_pRef(new GDrawAsm)
{
}

GDrawCairo::~GDrawCairo()
{
	// As GDrawAsm: never leave the render region pointing at a deleted context.
	if (this == GRenderRegion::GetStaticDrawContext())
		GRenderRegion::SetTempDrawContext(NULL);
	WriteStats();
	delete m_pRef;
}

BOOL GDrawCairo::Init()
{
	return m_pRef != NULL && m_pRef->Init();
}

eError GDrawCairo::GetLastError() { return m_pRef->GetLastError(); }

INT32 GDrawCairo::GetGraduationTableSize() { return m_pRef->GetGraduationTableSize(); }
INT32 GDrawCairo::GetGraduationTableLength() { return m_pRef->GetGraduationTableLength(); }
INT32 GDrawCairo::GetLongGraduationTableSize() { return m_pRef->GetLongGraduationTableSize(); }
INT32 GDrawCairo::GetLongGraduationTableLength() { return m_pRef->GetLongGraduationTableLength(); }
INT32 GDrawCairo::GetTransparentGraduationTableSize() { return m_pRef->GetTransparentGraduationTableSize(); }
INT32 GDrawCairo::GetTransparentGraduationTableLength() { return m_pRef->GetTransparentGraduationTableLength(); }
INT32 GDrawCairo::GetLongTransparentGraduationTableSize() { return m_pRef->GetLongTransparentGraduationTableSize(); }
INT32 GDrawCairo::GetLongTransparentGraduationTableLength() { return m_pRef->GetLongTransparentGraduationTableLength(); }

void GDrawCairo::SetStackLimit( LPVOID Limit ) { m_pRef->SetStackLimit(Limit); }
void GDrawCairo::SetStackSize( DWORD Size ) { m_pRef->SetStackSize(Size); }

// ---- target bitmap -----------------------------------------------------------------------

// Mirror a bitmap binding. GDraw resets the changed box and the clip rectangle when a
// bitmap is bound (measured), so the core does the same.
void GDrawCairo::BindTarget(BOOL Bound, const BITMAPINFOHEADER* pHeader, LPBYTE pBits)
{
	const bool Supported = Bound && pHeader != NULL && pBits != NULL &&
						   pHeader->biBitCount == 32 &&
						   (pHeader->biCompression == BI_RGB || pHeader->biCompression == 0x80000001) &&
						   pHeader->biPlanes == 1 && pHeader->biWidth > 0 && pHeader->biHeight > 0;
	if (Supported)
		m_Core.SetTarget(reinterpret_cast<std::uint32_t*>(pBits), pHeader->biWidth, pHeader->biHeight, kTopDownDIB,
						 pHeader->biCompression == 0x80000001);
	else
		m_Core.ClearTarget();
	// XARA_CAIRO_TRACE=1: log every bitmap binding (diagnosis only).
	static const bool s_bTrace = getenv("XARA_CAIRO_TRACE") != NULL;
	if (s_bTrace && pHeader != NULL)
		fprintf(stderr, "Xara renderer trace: bind %p %dx%d %dbpp compression %#x bits %s -> %s\n",
				static_cast<void*>(this), int(pHeader->biWidth), int(pHeader->biHeight), int(pHeader->biBitCount),
				unsigned(pHeader->biCompression), pBits ? "set" : "NULL", m_Core.HasTarget() ? "cairo" : "gdraw");
	m_Gate.Target(m_Core.HasTarget());
	m_Core.ClearChangedBBox();
	SyncClip();
}

BOOL GDrawCairo::SetupBitmap( INT32 Width, INT32 Height, INT32 Depth, LPBYTE Bits, DWORD Format, BOOL DoBiCompression )
{
	const BOOL Result = m_pRef->SetupBitmap(Width, Height, Depth, Bits, Format, DoBiCompression);
	BITMAPINFOHEADER Header;
	memset(&Header, 0, sizeof(Header));
	Header.biWidth = Width;
	Header.biHeight = Height;
	Header.biPlanes = 1;
	Header.biBitCount = WORD(Depth);
	Header.biCompression = DoBiCompression ? 0x80000001 : BI_RGB;
	BindTarget(Result, &Header, Bits);
	return Result;
}

BOOL GDrawCairo::SetBitmap( CONST LPBITMAPINFOHEADER BitmapInfo, CONST LPBYTE Bitmap, DWORD BitmapFormat )
{
	const BOOL Result = m_pRef->SetBitmap(BitmapInfo, Bitmap, BitmapFormat);
	BindTarget(Result, BitmapInfo, Bitmap);
	return Result;
}

// ---- transform and quality ---------------------------------------------------------------

BOOL GDrawCairo::SetMatrix( GMATRIX* GMat )
{
	const BOOL Result = m_pRef->SetMatrix(GMat);
	if (GMat != NULL)
		m_Core.SetMatrix(*reinterpret_cast<const XaraCairo::DeviceMatrix*>(GMat));
	return Result;
}

void GDrawCairo::SetAntialiasFlag( BOOL Flag )
{
	m_pRef->SetAntialiasFlag(Flag);
	m_Core.SetAntialias(Flag != FALSE);
}

void GDrawCairo::SetAntialiasQualityFlag( BOOL Flag ) { m_pRef->SetAntialiasQualityFlag(Flag); }
void GDrawCairo::SetTileSmoothingFlag( BOOL Flag ) { m_pRef->SetTileSmoothingFlag(Flag); }
void GDrawCairo::SetTileFilteringFlag( BOOL Flag ) { m_pRef->SetTileFilteringFlag(Flag); }
void GDrawCairo::SetMaxFilterSize( UINT32 Size ) { m_pRef->SetMaxFilterSize(Size); }
BOOL GDrawCairo::Sharpen( INT32 n ) { return m_pRef->Sharpen(n); }
BOOL GDrawCairo::Blur( INT32 n ) { return m_pRef->Blur(n); }

void GDrawCairo::SetHintingFlag( BOOL Flag )
{
	m_pRef->SetHintingFlag(Flag);
	m_Core.SetHinting(Flag != FALSE);
}

void GDrawCairo::SetFlatness( DWORD Flatness )
{
	m_pRef->SetFlatness(Flatness);
	m_Core.SetFlatness(Flatness);
}

BOOL GDrawCairo::SetMiterLimit( DWORD Limit )
{
	m_Gate.MiterLimitSet();		// GDraw's limit units are not verified: miter joins go to GDraw
	return m_pRef->SetMiterLimit(Limit);
}

BOOL GDrawCairo::SetMatrixIntFlag( BOOL Flag )
{
	m_Gate.MatrixIntFlag(Flag != FALSE);
	return m_pRef->SetMatrixIntFlag(Flag);
}

BOOL GDrawCairo::SetDashAdjustmentFlag( BOOL Flag ) { return m_pRef->SetDashAdjustmentFlag(Flag); }

void GDrawCairo::TransformPath( LPPOINT IPoints, LPPOINT OPoints, DWORD Length, GMATRIX* GMat )
{
	m_pRef->TransformPath(IPoints, OPoints, Length, GMat);
}

INT32 GDrawCairo::MaxScale( CONST GMATRIX *Matrix, CONST RECT *BBox ) { return m_pRef->MaxScale(Matrix, BBox); }

// ---- colour and paint ------------------------------------------------------------------
// A colour call replaces the whole GDraw paint (measured), so only the most recent paint
// call decides whether Cairo may draw.

void GDrawCairo::SetHalftoneOrigin( DWORD x, DWORD y ) { m_pRef->SetHalftoneOrigin(x, y); }
BOOL GDrawCairo::SetDitherStyle( DitherStyle Style ) { return m_pRef->SetDitherStyle(Style); }
BOOL GDrawCairo::AddToGraduationTable( COLORREF c, BOOL HSV, GraduationTable* t, DWORD i ) { return m_pRef->AddToGraduationTable(c, HSV, t, i); }
BOOL GDrawCairo::BuildGraduationTable( COLORREF a, COLORREF b, DWORD e, GraduationTable* t ) { return m_pRef->BuildGraduationTable(a, b, e, t); }
BOOL GDrawCairo::BuildTransparencyTable( DWORD a, DWORD b, TransparentGradTable* t ) { return m_pRef->BuildTransparencyTable(a, b, t); }

BOOL GDrawCairo::SetColour( COLORREF Colour )
{
	const BOOL Result = m_pRef->SetColour(Colour);
	if ((Colour >> 24) != 0)
		m_Gate.OtherPaint("colours carrying a transparency byte");
	else
	{
		m_Gate.SolidPaint();
		m_Core.SetColour(Colour);
	}
	return Result;
}

BOOL GDrawCairo::SetSolidColour( COLORREF Colour, DWORD BPP, DWORD Format )
{
	const BOOL Result = m_pRef->SetSolidColour(Colour, BPP, Format);
	if ((Colour >> 24) != 0 || !XaraCairo::Gate::IsExactSolidDepth(BPP))
		m_Gate.OtherPaint("solid colours quantised for a low-depth screen");
	else
	{
		m_Gate.SolidPaint();
		m_Core.SetColour(Colour);
	}
	return Result;
}

BOOL GDrawCairo::SetWordColour( DWORD Colour )
{
	return OtherPaint(m_pRef->SetWordColour(Colour), "raw word colours");
}

BOOL GDrawCairo::SetTransparency( COLORREF Colour, TransparencyEnum Style )
{
	const BOOL Result = m_pRef->SetTransparency(Colour, Style);
	if (XaraCairo::Gate::IsOpaqueTransparency(Colour, DWORD(Style)))
	{
		m_Gate.SolidPaint();
		m_Core.SetColour(Colour & 0x00ffffff, Style == T_REFLECTIVE);
	}
	else
		m_Gate.OtherPaint("transparency");
	return Result;
}

BOOL GDrawCairo::ConvertBitmap( LPBITMAPINFOHEADER a, LPBYTE b, LPBITMAPINFOHEADER c, LPBYTE d, INT32 e ) { return m_pRef->ConvertBitmap(a, b, c, d, e); }
const LOGPALETTE* GDrawCairo::SelectPalette( INT32 Flag ) { return m_pRef->SelectPalette(Flag); }

BOOL GDrawCairo::SetInvert( DWORD Colour )
{
	return OtherPaint(m_pRef->SetInvert(Colour), "inverted (XOR) paint");
}

BOOL GDrawCairo::InitialiseWithPalette( const LOGPALETTE* Palette ) { return m_pRef->InitialiseWithPalette(Palette); }
CONST BYTE* GDrawCairo::ReturnBrush() { return m_pRef->ReturnBrush(); }
CONST BYTE* GDrawCairo::ReturnBrushRGB() { return m_pRef->ReturnBrushRGB(); }

BOOL GDrawCairo::SetSeparationTables( const BGR *C, const BGR *M, const BGR *Y, const BGR *K,
									  const BYTE *UCR, const BYTE *BG )
{
	m_Gate.SeparationTables(C || M || Y || K || UCR || BG);
	return m_pRef->SetSeparationTables(C, M, Y, K, UCR, BG);
}

BOOL GDrawCairo::SetBitmapConversionTable( const BGR* Table )
{
	m_Gate.ConversionTable(Table != NULL);
	return m_pRef->SetBitmapConversionTable(Table);
}

BOOL GDrawCairo::SetGraduation( DWORD Style, CONST GraduationTable *Table,
								CONST POINT *A, CONST POINT *B, CONST POINT *C )
{
	return OtherPaint(m_pRef->SetGraduation(Style, Table, A, B, C), "gradient fills");
}

BOOL GDrawCairo::SetTransparentGraduation( DWORD Style, CONST TranspGradTable *Table,
										   CONST POINT *A, CONST POINT *B, CONST POINT *C )
{
	return OtherPaint(m_pRef->SetTransparentGraduation(Style, Table, A, B, C), "gradient transparency");
}

BOOL GDrawCairo::SetGraduation4( DWORD Style, CONST GraduationTable *Table,
								 CONST POINT *A, CONST POINT *B, CONST POINT *C, CONST POINT *D )
{
	return OtherPaint(m_pRef->SetGraduation4(Style, Table, A, B, C, D), "gradient fills");
}

BOOL GDrawCairo::SetTransparentGraduation4( DWORD Style, CONST TranspGradTable *Table,
											CONST POINT *A, CONST POINT *B, CONST POINT *C, CONST POINT *D )
{
	return OtherPaint(m_pRef->SetTransparentGraduation4(Style, Table, A, B, C, D), "gradient transparency");
}

// Bitmap fill/plot adjustments: GDraw only.
BOOL GDrawCairo::SetBias( UINT32 c, double v ) { return m_pRef->SetBias(c, v); }
BOOL GDrawCairo::SetGain( UINT32 c, double v ) { return m_pRef->SetGain(c, v); }
BOOL GDrawCairo::SetBrightness( double v ) { return m_pRef->SetBrightness(v); }
BOOL GDrawCairo::SetContrast( double v ) { return m_pRef->SetContrast(v); }
BOOL GDrawCairo::SetGamma( double v ) { return m_pRef->SetGamma(v); }
BOOL GDrawCairo::SetPostGamma( double v ) { return m_pRef->SetPostGamma(v); }
BOOL GDrawCairo::SetSaturation( double v ) { return m_pRef->SetSaturation(v); }
BOOL GDrawCairo::SetContone( UINT32 s, COLORREF a, COLORREF b ) { return m_pRef->SetContone(s, a, b); }
BOOL GDrawCairo::SetInputRange( UINT32 c, BYTE a, BYTE b ) { return m_pRef->SetInputRange(c, a, b); }
BOOL GDrawCairo::SetOutputRange( UINT32 c, BYTE a, BYTE b ) { return m_pRef->SetOutputRange(c, a, b); }

BOOL GDrawCairo::SetBitmapFill( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, COLORREF c, LPRGBQUAD q,
								LPBYTE r, LPBYTE g, LPBYTE bl, LPBYTE t, DWORD o )
{
	return OtherPaint(m_pRef->SetBitmapFill(bi, b, s, p, c, q, r, g, bl, t, o), "bitmap fills");
}

BOOL GDrawCairo::SetTransparentBitmapFill( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, COLORREF c, BYTE* t, DWORD o )
{
	return OtherPaint(m_pRef->SetTransparentBitmapFill(bi, b, s, p, c, t, o), "bitmap transparency");
}

BOOL GDrawCairo::SetPerspectiveBitmapFill( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, COLORREF c, LPRGBQUAD q,
										   LPBYTE r, LPBYTE g, LPBYTE bl, LPBYTE t, DWORD o )
{
	return OtherPaint(m_pRef->SetPerspectiveBitmapFill(bi, b, s, p, c, q, r, g, bl, t, o), "bitmap fills");
}

BOOL GDrawCairo::SetPerspectiveTransparentBitmapFill( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, COLORREF c, BYTE* t, DWORD o )
{
	return OtherPaint(m_pRef->SetPerspectiveTransparentBitmapFill(bi, b, s, p, c, t, o), "bitmap transparency");
}

BOOL GDrawCairo::SetTransparencyLookupTable( CONST BYTE * Table )
{
	m_Gate.TransparencyTable(Table != NULL);
	return m_pRef->SetTransparencyLookupTable(Table);
}

BOOL GDrawCairo::Set3WayGraduation( DWORD s, COLORREF a, COLORREF b, COLORREF d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pd )
{
	return OtherPaint(m_pRef->Set3WayGraduation(s, a, b, d, pa, pb, pd), "multi-colour gradient fills");
}

BOOL GDrawCairo::Set3WayGraduation4( DWORD s, COLORREF a, COLORREF b, COLORREF d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pc, CONST POINT *pd )
{
	return OtherPaint(m_pRef->Set3WayGraduation4(s, a, b, d, pa, pb, pc, pd), "multi-colour gradient fills");
}

BOOL GDrawCairo::Set4WayGraduation( DWORD s, COLORREF a, COLORREF b, COLORREF c, COLORREF d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pd )
{
	return OtherPaint(m_pRef->Set4WayGraduation(s, a, b, c, d, pa, pb, pd), "multi-colour gradient fills");
}

BOOL GDrawCairo::Set4WayGraduation4( DWORD s, COLORREF a, COLORREF b, COLORREF c, COLORREF d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pc, CONST POINT *pd )
{
	return OtherPaint(m_pRef->Set4WayGraduation4(s, a, b, c, d, pa, pb, pc, pd), "multi-colour gradient fills");
}

BOOL GDrawCairo::SetTransparent3WayGraduation( DWORD s, BYTE a, BYTE b, BYTE d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pd )
{
	return OtherPaint(m_pRef->SetTransparent3WayGraduation(s, a, b, d, pa, pb, pd), "gradient transparency");
}

BOOL GDrawCairo::SetTransparent3WayGraduation4( DWORD s, BYTE a, BYTE b, BYTE d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pc, CONST POINT *pd )
{
	return OtherPaint(m_pRef->SetTransparent3WayGraduation4(s, a, b, d, pa, pb, pc, pd), "gradient transparency");
}

BOOL GDrawCairo::SetTransparent4WayGraduation( DWORD s, BYTE a, BYTE b, BYTE c, BYTE d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pd )
{
	return OtherPaint(m_pRef->SetTransparent4WayGraduation(s, a, b, c, d, pa, pb, pd), "gradient transparency");
}

BOOL GDrawCairo::SetTransparent4WayGraduation4( DWORD s, BYTE a, BYTE b, BYTE c, BYTE d, CONST POINT *pa, CONST POINT *pb, CONST POINT *pc, CONST POINT *pd )
{
	return OtherPaint(m_pRef->SetTransparent4WayGraduation4(s, a, b, c, d, pa, pb, pc, pd), "gradient transparency");
}

// ---- drawing -----------------------------------------------------------------------------

BOOL GDrawCairo::FillPath( LPPOINT Points, const BYTE* Types, UINT32 Length, DWORD Winding )
{
	const StatTimer Timer;
	const char* pReason = m_Gate.Blocker();
	if (pReason == NULL)
	{
		if (m_Core.FillPath(reinterpret_cast<const XaraCairo::DevicePoint*>(Points), Types, Length, Winding) == XaraCairo::DRAWN)
			return Drawn(OP_FILL_PATH, Timer);
		pReason = m_Core.LastUnsupportedReason();
	}
	const BOOL Result = m_pRef->FillPath(Points, Types, Length, Winding);
	Delegated(OP_FILL_PATH, pReason, Timer);
	return Result;
}

BOOL GDrawCairo::StrokePath( LPPOINT Points, const BYTE* Types, UINT32 Length, BOOL Close, DWORD LineWidth,
							 CapStyles Caps, JoinStyles Join, const DashType* Dash )
{
	const StatTimer Timer;
	const char* pReason = m_Gate.Blocker();
	if (pReason == NULL && Dash != NULL && Dash->Length != 0)
		pReason = "dashed strokes";
	if (pReason == NULL && Join == JOIN_MITER && m_Gate.MiterLimitChanged())
		pReason = "a changed miter limit";
	if (pReason == NULL)
	{
		if (m_Core.StrokePath(reinterpret_cast<const XaraCairo::DevicePoint*>(Points), Types, Length, Close != FALSE,
							  LineWidth, XaraCairo::CapStyle(Caps), XaraCairo::JoinStyle(Join)) == XaraCairo::DRAWN)
			return Drawn(OP_STROKE_PATH, Timer);
		pReason = m_Core.LastUnsupportedReason();
	}
	const BOOL Result = m_pRef->StrokePath(Points, Types, Length, Close, LineWidth, Caps, Join, Dash);
	Delegated(OP_STROKE_PATH, pReason, Timer);
	return Result;
}

BOOL GDrawCairo::FillRectangle( LPRECT Rect )
{
	const StatTimer Timer;
	const char* pReason = m_Gate.Blocker();
	if (pReason == NULL && Rect != NULL)
	{
		if (m_Core.FillRectangle(*reinterpret_cast<const XaraCairo::DeviceRect*>(Rect)) == XaraCairo::DRAWN)
			return Drawn(OP_FILL_RECTANGLE, Timer);
		pReason = m_Core.LastUnsupportedReason();
	}
	const BOOL Result = m_pRef->FillRectangle(Rect);
	Delegated(OP_FILL_RECTANGLE, pReason, Timer);
	return Result;
}

BOOL GDrawCairo::FillPoint( LPPOINT Point )
{
	const StatTimer Timer;
	const char* pReason = m_Gate.Blocker();
	if (pReason == NULL && Point != NULL)
	{
		if (m_Core.FillPoint(*reinterpret_cast<const XaraCairo::DevicePoint*>(Point)) == XaraCairo::DRAWN)
			return Drawn(OP_FILL_POINT, Timer);
		pReason = m_Core.LastUnsupportedReason();
	}
	const BOOL Result = m_pRef->FillPoint(Point);
	Delegated(OP_FILL_POINT, pReason, Timer);
	return Result;
}

BOOL GDrawCairo::FillRegion( CONST REGION *Region, CONST POINT *Offset )
{
	const StatTimer Timer;
	const BOOL Result = m_pRef->FillRegion(Region, Offset);
	Delegated(OP_FILL_REGION, "regions", Timer);
	return Result;
}

BOOL GDrawCairo::PlotBitmap( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, LPRGBQUAD q,
							 LPBYTE r, LPBYTE g, LPBYTE bl, LPBYTE t )
{
	const StatTimer Timer;
	const BOOL Result = m_pRef->PlotBitmap(bi, b, s, p, q, r, g, bl, t);
	Delegated(OP_PLOT_BITMAP, "bitmaps", Timer);
	return Result;
}

BOOL GDrawCairo::PlotPerspectiveBitmap( LPBITMAPINFOHEADER bi, LPBYTE b, DWORD s, LPPOINT p, LPRGBQUAD q,
										LPBYTE r, LPBYTE g, LPBYTE bl, LPBYTE t )
{
	const StatTimer Timer;
	const BOOL Result = m_pRef->PlotPerspectiveBitmap(bi, b, s, p, q, r, g, bl, t);
	Delegated(OP_PLOT_BITMAP, "perspective bitmaps", Timer);
	return Result;
}

// ---- geometry (GDraw in this stage) --------------------------------------------------------

INT32 GDrawCairo::StrokePathToPath( CONST POINT *IPoints, CONST BYTE *ITypes, DWORD ILength,
									POINT *OPoints, BYTE *OTypes, DWORD OLength, BOOL Close, DWORD LineWidth,
									CapStyles LineCaps, JoinStyles LineJoin, CONST DashType *Dash )
{
	return m_pRef->StrokePathToPath(IPoints, ITypes, ILength, OPoints, OTypes, OLength, Close, LineWidth, LineCaps, LineJoin, Dash);
}

INT32 GDrawCairo::CalcStrokeBBox( CONST POINT *IPoints, CONST BYTE *ITypes, DWORD ILength, LPRECT Rect,
								  BOOL Close, DWORD LineWidth, CapStyles LineCaps, JoinStyles LineJoin,
								  CONST DashType *Dash )
{
	return m_pRef->CalcStrokeBBox(IPoints, ITypes, ILength, Rect, Close, LineWidth, LineCaps, LineJoin, Dash);
}

BOOL GDrawCairo::HintPath( CONST POINT *Points, CONST BYTE *Types, UINT32 Length, BOOL Close, DWORD LineWidth )
{
	return m_pRef->HintPath(Points, Types, Length, Close, LineWidth);
}

INT32 GDrawCairo::MakeUnclippedRegion( CONST POINT *Points, CONST BYTE *Types, UINT32 Length, DWORD Winding,
									   REGION *ORegion, UINT32 OLength )
{
	return m_pRef->MakeUnclippedRegion(Points, Types, Length, Winding, ORegion, OLength);
}

INT32 GDrawCairo::SetBevelContrast( UINT32 n ) { return m_pRef->SetBevelContrast(n); }
INT32 GDrawCairo::SetBevelLightness( UINT32 n ) { return m_pRef->SetBevelLightness(n); }
INT32 GDrawCairo::SetBevelDarkness( UINT32 n ) { return m_pRef->SetBevelDarkness(n); }
INT32 GDrawCairo::TranslateBevelValue( BYTE i, BYTE c ) { return m_pRef->TranslateBevelValue(i, c); }

BOOL GDrawCairo::IsOverlap( LPPOINT p, const BYTE * t, UINT32 n, DWORD w ) { return m_pRef->IsOverlap(p, t, n, w); }
BOOL GDrawCairo::GetStatistics( LPPOINT p, const BYTE * t, UINT32 n, DWORD w, STATISTICS * s ) { return m_pRef->GetStatistics(p, t, n, w, s); }
BOOL GDrawCairo::CalcBBox( LPPOINT p, const BYTE * t, DWORD n, LPRECT r, BOOL f ) { return m_pRef->CalcBBox(p, t, n, r, f); }
INT32 GDrawCairo::MakeRegion( LPPOINT p, const BYTE * t, UINT32 n, DWORD w, REGION * r, UINT32 l ) { return m_pRef->MakeRegion(p, t, n, w, r, l); }

// ---- clipping ------------------------------------------------------------------------------
// GDraw computes the effective clip; the core uses GDraw's resulting device rectangle.

void GDrawCairo::SyncClip()
{
	RECT Clip;
	if (m_pRef->GetDeviceClipRectangle(&Clip))
		m_Core.SetDeviceClip(*reinterpret_cast<const XaraCairo::DeviceRect*>(&Clip));
	else
		m_Core.SetDeviceClip(XaraCairo::Renderer::EmptyRect());	// unknown clip: draw nothing
}

BOOL GDrawCairo::ClipRectangle( LPRECT Rect )
{
	const BOOL Result = m_pRef->ClipRectangle(Rect);
	SyncClip();
	return Result;
}

BOOL GDrawCairo::DeviceClipRectangle( LPRECT Rect )
{
	// GDraw's DeviceClipRectangle also clears any clip region (see ApplyCurrentClipRegion).
	const BOOL Result = m_pRef->DeviceClipRectangle(Rect);
	m_Gate.RegionClip(false);
	SyncClip();
	return Result;
}

BOOL GDrawCairo::GetDeviceClipRectangle( LPRECT Rect ) { return m_pRef->GetDeviceClipRectangle(Rect); }

BOOL GDrawCairo::ClipRegion( const REGION* Region )
{
	const BOOL Result = m_pRef->ClipRegion(Region);
	m_Gate.RegionClip(Region != NULL);
	SyncClip();
	return Result;
}

const REGION* GDrawCairo::GetClipRegion() { return m_pRef->GetClipRegion(); }

// ---- changed box: union of what GDraw and Cairo drew ---------------------------------------

void GDrawCairo::ClearChangedBBox()
{
	m_pRef->ClearChangedBBox();
	m_Core.ClearChangedBBox();
}

BOOL GDrawCairo::GetChangedBBox( LPRECT Rect )
{
	const BOOL Result = m_pRef->GetChangedBBox(Rect);
	if (Result && Rect != NULL)
	{
		const XaraCairo::DeviceRect Union = XaraCairo::Renderer::UnionRect(
			*reinterpret_cast<const XaraCairo::DeviceRect*>(Rect), m_Core.GetChangedBBox());
		*Rect = *reinterpret_cast<const RECT*>(&Union);
	}
	return Result;
}

BOOL GDrawCairo::SetChangedBBox( LPRECT Rect )
{
	m_Core.ClearChangedBBox();
	return m_pRef->SetChangedBBox(Rect);
}

// ---- misc ----------------------------------------------------------------------------------

BOOL GDrawCairo::SetConversionPalette( PLOGPALETTE Palette ) { return m_pRef->SetConversionPalette(Palette); }
COLORREF GDrawCairo::ConvertHSVtoRGB( COLORREF c ) { return m_pRef->ConvertHSVtoRGB(c); }
COLORREF GDrawCairo::ConvertRGBtoHSV( COLORREF c ) { return m_pRef->ConvertRGBtoHSV(c); }
DWORD GDrawCairo::GetMaxBitmapWidth(void) { return m_pRef->GetMaxBitmapWidth(); }
DWORD GDrawCairo::GetMaxBitmapDepth(void) { return m_pRef->GetMaxBitmapDepth(); }

BOOL GDrawCairo::SaveContext( GCONTEXT* )
{
	ERROR3("GDrawCairo::SaveContext - raw GDraw context snapshots are not supported");
	return FALSE;
}

BOOL GDrawCairo::RestoreContext( CONST GCONTEXT* )
{
	ERROR3("GDrawCairo::RestoreContext - raw GDraw context snapshots are not supported");
	return FALSE;
}
