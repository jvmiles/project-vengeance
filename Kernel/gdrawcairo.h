// SPDX-License-Identifier: GPL-2.0-only
// GDrawContext backed by Cairo for a verified subset, GDraw for everything else.
#ifndef XARA_GDRAW_CAIRO_H
#define XARA_GDRAW_CAIRO_H

#include <chrono>

#include "gdrawcon.h"
#include "cairocore.h"

class GDrawAsm;

/*******************************************************************************************

>	class GDrawCairo : public GDrawContext

	Purpose:	Stage 2A Cairo renderer (selected with XARA_RENDERER=cairo).

				Every state call (bitmap, matrix, colours, fills, clipping, tables, flags)
				is forwarded to an internal GDrawAsm, which therefore always holds the
				complete GDraw state, and mirrored into a XaraCairo::Gate/Renderer.

				A draw call is rendered by Cairo only when the gate says the current state
				is in the verified subset (32bpp BI_RGB target, opaque solid colour, no
				clip region or colour tables) and the core accepts the request (even-odd or
				non-zero fill, undashed stroke under a uniform matrix, rectangle under an
				axis-aligned matrix, point). Otherwise the GDraw context draws it into the
				same caller-owned bitmap and the reason is reported once per process.

				Geometry queries (bounds, stroke-to-path, regions, statistics), tables,
				palettes and bitmap conversion stay with GDraw in this stage.

	SeeAlso:	GDrawAsm, XaraCairo::Renderer, docs/GDRAW_CAIRO_STAGE2A.md

********************************************************************************************/

class GDrawCairo : public GDrawContext
{
public:
	GDrawCairo();
	virtual ~GDrawCairo();

	virtual BOOL Init();

	virtual eError GetLastError();

	virtual INT32 GetGraduationTableSize();
	virtual INT32 GetGraduationTableLength();
	virtual INT32 GetLongGraduationTableSize();
	virtual INT32 GetLongGraduationTableLength();
	virtual INT32 GetTransparentGraduationTableSize();
	virtual INT32 GetTransparentGraduationTableLength();
	virtual INT32 GetLongTransparentGraduationTableSize();
	virtual INT32 GetLongTransparentGraduationTableLength();

	virtual void SetStackLimit( LPVOID );
	virtual void SetStackSize( DWORD );

	virtual BOOL SetupBitmap( INT32 Width, INT32 Height, INT32 Depth, LPBYTE Bits, DWORD Format = 2, BOOL DoBiCompression = FALSE );
	virtual BOOL SetBitmap( CONST LPBITMAPINFOHEADER BitmapInfo, CONST LPBYTE Bitmap, DWORD BitmapFormat = 2);

	virtual BOOL SetMatrix( GMATRIX* );
	virtual void SetAntialiasFlag( BOOL );
	virtual void SetAntialiasQualityFlag( BOOL );
	virtual void SetTileSmoothingFlag( BOOL );
	virtual void SetTileFilteringFlag( BOOL );
	virtual void SetMaxFilterSize( UINT32 );
	virtual BOOL Sharpen( INT32 );
	virtual BOOL Blur( INT32 );
	virtual void SetHintingFlag( BOOL );
	virtual void SetFlatness( DWORD );

	virtual BOOL SetMiterLimit( DWORD );
	virtual BOOL SetMatrixIntFlag( BOOL );
	virtual BOOL SetDashAdjustmentFlag( BOOL );

	virtual void TransformPath( LPPOINT, LPPOINT, DWORD, GMATRIX* );
	virtual INT32 MaxScale( CONST GMATRIX *Matrix, CONST RECT *BBox );

	// colour
	virtual void SetHalftoneOrigin( DWORD, DWORD );
	virtual BOOL SetDitherStyle(DitherStyle Style = DITHER_GREY_ORDERED);
	virtual BOOL AddToGraduationTable( COLORREF, BOOL, GraduationTable*, DWORD );
	virtual BOOL BuildGraduationTable( COLORREF, COLORREF, DWORD, GraduationTable* );
	virtual BOOL BuildTransparencyTable( DWORD, DWORD, TransparentGradTable* );
	virtual BOOL SetColour( COLORREF );
	virtual BOOL SetSolidColour( COLORREF, DWORD BPP = 0, DWORD Format = 2 );
	virtual BOOL SetWordColour( DWORD );
	virtual BOOL SetTransparency( COLORREF, TransparencyEnum );
	virtual BOOL ConvertBitmap( LPBITMAPINFOHEADER, LPBYTE, LPBITMAPINFOHEADER, LPBYTE, INT32 );
	virtual const LOGPALETTE* SelectPalette( INT32 );

	virtual BOOL SetInvert( DWORD );
	virtual BOOL InitialiseWithPalette( const LOGPALETTE* );
	virtual CONST BYTE* ReturnBrush();
	virtual CONST BYTE* ReturnBrushRGB();

	virtual BOOL SetSeparationTables(const BGR *CyanSepTable = NULL,
									 const BGR *MagentaSepTable = NULL,
									 const BGR *YellowSepTable = NULL,
									 const BGR *BlackSepTable = NULL,
									 const BYTE *UnderColourRemovalTable = NULL,
									 const BYTE *BlackGenerationTable = NULL);

	virtual BOOL SetBitmapConversionTable( const BGR* BitmapConversionTable = NULL );

	virtual BOOL SetGraduation( DWORD Style, CONST GraduationTable *Table,
								CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC );
	virtual BOOL SetTransparentGraduation( DWORD Style, CONST TranspGradTable *Table,
										   CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC );
	virtual BOOL SetGraduation4( DWORD Style, CONST GraduationTable *Table,
								 CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD );
	virtual BOOL SetTransparentGraduation4( DWORD Style, CONST TranspGradTable *Table,
											CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD );

	virtual BOOL SetBias(UINT32,double);
	virtual BOOL SetGain(UINT32,double);
	virtual BOOL SetBrightness(double);
	virtual BOOL SetContrast(double);
	virtual BOOL SetGamma(double);
	virtual BOOL SetPostGamma(double);
	virtual BOOL SetSaturation(double);
	virtual BOOL SetContone( UINT32, COLORREF=0x000000, COLORREF=0xFFFFFF );
	virtual BOOL SetInputRange( UINT32,BYTE=0x00,BYTE=0xFF );
	virtual BOOL SetOutputRange( UINT32,BYTE=0x00,BYTE=0xFF );

	virtual BOOL SetBitmapFill( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, COLORREF, LPRGBQUAD,
								LPBYTE, LPBYTE, LPBYTE, LPBYTE, DWORD = 0);
	virtual BOOL SetTransparentBitmapFill( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, COLORREF, BYTE* = NULL, DWORD = 0);
	virtual BOOL SetPerspectiveBitmapFill( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, COLORREF, LPRGBQUAD,
										   LPBYTE, LPBYTE, LPBYTE, LPBYTE, DWORD );
	virtual BOOL SetPerspectiveTransparentBitmapFill( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, COLORREF, BYTE*, DWORD);

	virtual BOOL SetTransparencyLookupTable(CONST BYTE * Table);

	// rendering
	virtual BOOL PlotBitmap( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, LPRGBQUAD,
							 LPBYTE, LPBYTE, LPBYTE, LPBYTE);
	virtual BOOL PlotPerspectiveBitmap( LPBITMAPINFOHEADER, LPBYTE, DWORD, LPPOINT, LPRGBQUAD,
										LPBYTE, LPBYTE, LPBYTE, LPBYTE);

	virtual BOOL StrokePath( LPPOINT, const BYTE *, UINT32, BOOL, DWORD, CapStyles, JoinStyles, const DashType* );
	virtual BOOL FillPath( LPPOINT, const BYTE *, UINT32, DWORD );
	virtual BOOL FillRectangle( LPRECT );
	virtual BOOL FillPoint( LPPOINT );
	virtual INT32 StrokePathToPath( CONST POINT *IPoints, CONST BYTE *ITypes, DWORD ILength,
									POINT *OPoints, BYTE *OTypes, DWORD OLength, BOOL Close, DWORD LineWidth,
									CapStyles LineCaps, JoinStyles LineJoin, CONST DashType *Dash);
	virtual INT32 CalcStrokeBBox( CONST POINT *IPoints, CONST BYTE *ITypes, DWORD ILength, LPRECT Rect,
								  BOOL Close, DWORD LineWidth, CapStyles LineCaps, JoinStyles LineJoin,
								  CONST DashType *Dash);
	virtual BOOL HintPath( CONST POINT *Points, CONST BYTE *Types, UINT32 Length, BOOL Close, DWORD LineWidth);
	virtual INT32 MakeUnclippedRegion( CONST POINT *Points, CONST BYTE *Types, UINT32 Length, DWORD Winding,
									   REGION *ORegion, UINT32 OLength);
	virtual BOOL FillRegion( CONST REGION *Region, CONST POINT *Offset );

	virtual INT32 SetBevelContrast(UINT32 nContrast);
	virtual INT32 SetBevelLightness(UINT32 nContrast);
	virtual INT32 SetBevelDarkness(UINT32 nContrast);
	virtual INT32 TranslateBevelValue(BYTE nIndex, BYTE Colour);

	// misc
	virtual BOOL IsOverlap( LPPOINT, const BYTE *, UINT32, DWORD);
	virtual BOOL GetStatistics( LPPOINT, const BYTE *, UINT32, DWORD, STATISTICS *);
	virtual BOOL CalcBBox( LPPOINT, const BYTE *, DWORD, LPRECT, BOOL);
	virtual INT32 MakeRegion( LPPOINT, const BYTE *, UINT32, DWORD, REGION *, UINT32);

	virtual BOOL ClipRectangle( LPRECT );
	virtual BOOL DeviceClipRectangle( LPRECT );
	virtual BOOL GetDeviceClipRectangle( LPRECT );
	virtual BOOL ClipRegion( const REGION* );
	virtual const REGION* GetClipRegion();

	virtual void ClearChangedBBox();
	virtual BOOL GetChangedBBox( LPRECT );
	virtual BOOL SetChangedBBox( LPRECT );

	virtual BOOL SetConversionPalette( PLOGPALETTE );

	virtual COLORREF ConvertHSVtoRGB(COLORREF);
	virtual COLORREF ConvertRGBtoHSV(COLORREF);

	virtual DWORD GetMaxBitmapWidth(void);
	virtual DWORD GetMaxBitmapDepth(void);

	virtual BOOL Set3WayGraduation(DWORD Style, COLORREF ColourA, COLORREF ColourB, COLORREF ColourD,
								   CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointD);
	virtual BOOL Set3WayGraduation4(DWORD Style, COLORREF ColourA, COLORREF ColourB, COLORREF ColourD,
									CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD);
	virtual BOOL Set4WayGraduation(DWORD Style, COLORREF ColourA, COLORREF ColourB, COLORREF ColourC, COLORREF ColourD,
								   CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointD);
	virtual BOOL Set4WayGraduation4(DWORD Style, COLORREF ColourA, COLORREF ColourB, COLORREF ColourC, COLORREF ColourD,
									CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD);
	virtual BOOL SetTransparent3WayGraduation(DWORD Style, BYTE ValueA, BYTE ValueB, BYTE ValueD,
											  CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointD);
	virtual BOOL SetTransparent3WayGraduation4(DWORD Style, BYTE ValueA, BYTE ValueB, BYTE ValueD,
											   CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD);
	virtual BOOL SetTransparent4WayGraduation(DWORD Style, BYTE ValueA, BYTE ValueB, BYTE ValueC, BYTE ValueD,
											  CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointD);
	virtual BOOL SetTransparent4WayGraduation4(DWORD Style, BYTE ValueA, BYTE ValueB, BYTE ValueC, BYTE ValueD,
											   CONST POINT *PointA, CONST POINT *PointB, CONST POINT *PointC, CONST POINT *PointD);

	// Raw GCONTEXT snapshots cannot describe the Cairo-side state; unsupported.
	virtual BOOL SaveContext(GCONTEXT* Context);
	virtual BOOL RestoreContext(CONST GCONTEXT* Context);

private:
	enum DrawOp { OP_FILL_PATH, OP_STROKE_PATH, OP_FILL_RECTANGLE, OP_FILL_POINT,
				  OP_FILL_REGION, OP_PLOT_BITMAP, OP_COUNT };

	// Wall time of one draw call, measured only when XARA_CAIRO_STATS is set.
	class StatTimer
	{
	public:
		StatTimer();
		double Micros() const;
	private:
		std::chrono::steady_clock::time_point m_Start;
	};

	BOOL Drawn(DrawOp Op, const StatTimer& Timer);
	void Delegated(DrawOp Op, const char* pReason, const StatTimer& Timer);
	void BindTarget(BOOL Bound, const BITMAPINFOHEADER* pHeader, LPBYTE pBits);
	void SyncClip();
	BOOL OtherPaint(BOOL Result, const char* pReason) { m_Gate.OtherPaint(pReason); return Result; }

	GDrawAsm* m_pRef;
	XaraCairo::Renderer m_Core;
	XaraCairo::Gate m_Gate;
};

#endif
