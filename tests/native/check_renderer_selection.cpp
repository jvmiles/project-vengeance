// SPDX-License-Identifier: GPL-2.0-only
// Unit test for the XARA_RENDERER selection policy used by GDrawFactory.
// Build: g++ -std=c++17 -Wall -Wextra -I Kernel tests/native/check_renderer_selection.cpp
#include "rendererselect.h"
#include <cstdio>
#include <cstring>

static int Failures = 0;

static void Check(bool Condition, const char* pWhat)
{
	if (!Condition)
	{
		std::printf("FAIL: %s\n", pWhat);
		Failures++;
	}
}

static void CheckSelection(const char* pValue, bool CairoAvailable, XaraRendererKind Requested,
						   XaraRendererKind Effective, bool Recognised, bool FellBack,
						   const char* pWhat)
{
	XaraRendererSelection Sel = XaraResolveRenderer(pValue, CairoAvailable);
	Check(Sel.Requested == Requested, pWhat);
	Check(Sel.Effective == Effective, pWhat);
	Check(Sel.Recognised == Recognised, pWhat);
	Check(Sel.FellBack == FellBack, pWhat);
}

static bool Contains(const char* pText, const char* pPart)
{
	return std::strstr(pText, pPart) != NULL;
}

int main()
{
	const XaraRendererKind G = XARA_RENDERER_GDRAW, C = XARA_RENDERER_CAIRO;

	// Default and explicit gdraw.
	CheckSelection(NULL, false, G, G, true, false, "unset selects gdraw");
	CheckSelection("", false, G, G, true, false, "empty selects gdraw");
	CheckSelection("gdraw", false, G, G, true, false, "gdraw selects gdraw");
	CheckSelection("gdraw", true, G, G, true, false, "gdraw ignores cairo availability");

	// Cairo: falls back while unavailable, selected once available.
	CheckSelection("cairo", false, C, G, true, true, "cairo unavailable falls back to gdraw");
	CheckSelection("cairo", true, C, C, true, false, "cairo available selects cairo");

	// Unknown values: never select anything but gdraw.
	const char* Unknown[] = { "Cairo", "GDRAW", " gdraw", "gdraw ", "cdraw", "native", "skia", "1", "cairo2" };
	for (size_t i = 0; i < sizeof(Unknown) / sizeof(Unknown[0]); i++)
	{
		CheckSelection(Unknown[i], false, G, G, false, true, Unknown[i]);
		CheckSelection(Unknown[i], true, G, G, false, true, Unknown[i]);
	}

	// Diagnostics.
	char Buf[256];
	XaraRendererSelection Sel;

	Sel = XaraResolveRenderer(NULL, false);
	Check(!XaraRendererDiagnostic(Sel, NULL, Buf, sizeof(Buf)) && Buf[0] == '\0', "default is quiet");
	Sel = XaraResolveRenderer("gdraw", false);
	Check(!XaraRendererDiagnostic(Sel, "gdraw", Buf, sizeof(Buf)) && Buf[0] == '\0', "explicit gdraw is quiet");
	Sel = XaraResolveRenderer("cairo", true);
	Check(!XaraRendererDiagnostic(Sel, "cairo", Buf, sizeof(Buf)), "available cairo is quiet");

	Sel = XaraResolveRenderer("cairo", false);
	Check(XaraRendererDiagnostic(Sel, "cairo", Buf, sizeof(Buf)), "cairo fallback is reported");
	Check(Contains(Buf, "XARA_RENDERER=cairo"), "cairo message names the request");
	Check(Contains(Buf, "not available"), "cairo message says unavailable");
	Check(Contains(Buf, "falling back to gdraw"), "cairo message names the fallback");
	std::printf("cairo diagnostic:   %s\n", Buf);

	Sel = XaraResolveRenderer("skia", false);
	Check(XaraRendererDiagnostic(Sel, "skia", Buf, sizeof(Buf)), "unknown value is reported");
	Check(Contains(Buf, "warning"), "unknown message is a warning");
	Check(Contains(Buf, "'skia'"), "unknown message quotes the invalid value");
	Check(Contains(Buf, "falling back to gdraw"), "unknown message names the fallback");
	std::printf("unknown diagnostic: %s\n", Buf);

	// Truncation is safe for oversized values.
	char Long[1024];
	std::memset(Long, 'x', sizeof(Long) - 1);
	Long[sizeof(Long) - 1] = '\0';
	char Small[32];
	Sel = XaraResolveRenderer(Long, false);
	Check(XaraRendererDiagnostic(Sel, Long, Small, sizeof(Small)), "long value is reported");
	Check(std::strlen(Small) == sizeof(Small) - 1, "long value is truncated within the buffer");

	if (Failures != 0)
	{
		std::printf("%d check(s) failed\n", Failures);
		return 1;
	}
	std::printf("PASS: renderer selection (default, gdraw, cairo fallback, unknown values, diagnostics)\n");
	return 0;
}
