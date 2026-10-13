// SPDX-License-Identifier: GPL-2.0-only
// Standalone access to the GDraw/CDraw public API for reference/differential tests.
// Base types follow wxOil/compatdef.h (x86_64 Linux) so the real GPL headers
// GDraw/gconsts.h and GDraw/gdraw.h can be included without wxWidgets.
// Tests using this link libs/x86_64/libCDraw.a; this is a test harness inside the
// Xara LX derivative, not a redistribution of the library.
#ifndef XARA_TEST_GDRAW_REFERENCE_H
#define XARA_TEST_GDRAW_REFERENCE_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef std::uint8_t BYTE;
typedef std::uint16_t WORD;
typedef std::uint32_t DWORD;
typedef std::uint32_t UINT32;
typedef std::int32_t INT32;
typedef std::uint32_t COLORREF;
typedef std::int64_t __int64;
typedef char CHAR;
typedef void VOID;
struct POINT { INT32 x, y; };
struct RECT { INT32 left, top, right, bottom; };
struct RGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; };
struct PALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; };
struct LOGPALETTE { WORD palVersion; WORD palNumEntries; PALETTEENTRY palPalEntry[1]; };
struct BITMAPINFOHEADER
{
	WORD biSize; INT32 biWidth; INT32 biHeight; WORD biPlanes; WORD biBitCount;
	DWORD biCompression; DWORD biSizeImage; INT32 biXPelsPerMeter; INT32 biYPelsPerMeter;
	DWORD biClrUsed; DWORD biClrImportant;
};
struct BITMAP;
typedef RGBQUAD* LPRGBQUAD;
const DWORD BI_RGB = 0;
inline COLORREF RGB(BYTE r, BYTE g, BYTE b) { return COLORREF(r) | (COLORREF(g) << 8) | (COLORREF(b) << 16); }

#include "gconsts.h"
#include "gdraw.h"

// Owns one initialised CDraw context and a caller-owned 32bpp DIB (BI_RGB, or 0x80000001
// for the transparency-channel format).
class RefGDraw
{
public:
	RefGDraw(INT32 Width, INT32 Height, bool Inverted = true, DWORD Fill = 0, DWORD Compression = BI_RGB)
		: m_Width(Width), m_Height(Height), m_Bits(size_t(Width) * Height, Fill)
	{
		m_pContext = static_cast<GCONTEXT*>(std::calloc(1, GDraw_ContextLength()));
		GDraw_Initialise(m_pContext, NULL);
		GDraw_SetMemoryHandlers(m_pContext, Alloc, Free);
		std::memset(&m_Header, 0, sizeof(m_Header));
		m_Header.biSize = sizeof(BITMAPINFOHEADER);
		m_Header.biWidth = Width;
		m_Header.biHeight = Height;
		m_Header.biPlanes = 1;
		m_Header.biBitCount = 32;
		m_Header.biCompression = Compression;
		m_Header.biXPelsPerMeter = m_Header.biYPelsPerMeter = 1000;
		BYTE* pBits = reinterpret_cast<BYTE*>(m_Bits.data());
		m_SetResult = Inverted ? GDraw_SetInvertedDIBitmap(m_pContext, &m_Header, pBits, 2)
							   : GDraw_SetDIBitmap(m_pContext, &m_Header, pBits, 2);
	}
	~RefGDraw()
	{
		GDraw_Terminate(m_pContext);
		std::free(m_pContext);
	}
	GCONTEXT* Context() { return m_pContext; }
	DWORD* Bits() { return m_Bits.data(); }
	const BITMAPINFOHEADER& Header() const { return m_Header; }
	INT32 SetResult() const { return m_SetResult; }
	// Memory row r (row 0 = first row in memory).
	DWORD Pixel(INT32 x, INT32 r) const { return m_Bits[size_t(r) * m_Width + x]; }

private:
	static BYTE* Alloc(UINT32 Size) { return static_cast<BYTE*>(std::malloc(Size)); }
	static void Free(BYTE* p) { std::free(p); }

	INT32 m_Width, m_Height;
	std::vector<DWORD> m_Bits;
	GCONTEXT* m_pContext;
	BITMAPINFOHEADER m_Header;
	INT32 m_SetResult;
};

// Device position (pixels) = (A*x + B*y + C) / 2^(FX+16), y up from the bitmap bottom.
// Tests use 1024 document units per pixel: A = 2^(FX+16)/1024 = 2^20.
const INT32 kRefUnitsPerPixel = 1024;
inline GMATRIX RefPixelMatrix(INT32 OriginXUnits = 0, INT32 OriginYUnits = 0)
{
	GMATRIX m;
	m.AX = 1 << (FX + 16 - 10);
	m.AY = 0;
	m.BX = 0;
	m.BY = m.AX;
	m.CX = __int64(OriginXUnits) * m.AX;
	m.CY = __int64(OriginYUnits) * m.AX;
	return m;
}

#endif
