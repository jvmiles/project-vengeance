// SPDX-License-Identifier: GPL-2.0-only
// Renderer selection policy for the XARA_RENDERER environment variable.
// Dependency-free so it can be unit tested without linking the application.
#ifndef XARA_RENDERER_SELECT_H
#define XARA_RENDERER_SELECT_H

#include <cstddef>
#include <cstdio>
#include <cstring>

enum XaraRendererKind
{
	XARA_RENDERER_GDRAW,			// Original CDraw backend via GDrawAsm (default)
	XARA_RENDERER_CAIRO				// GDrawCairo; not implemented yet
};

struct XaraRendererSelection
{
	XaraRendererKind Requested;		// What the user asked for (GDRAW if unset/unknown)
	XaraRendererKind Effective;		// What will actually be constructed
	bool Recognised;				// False if the value was set but not understood
	bool FellBack;					// True if Effective differs from what was asked for
};

inline const char* XaraRendererName(XaraRendererKind Kind)
{
	return (Kind == XARA_RENDERER_CAIRO) ? "cairo" : "gdraw";
}

// Resolve an XARA_RENDERER value. NULL or "" select gdraw silently. Values are
// matched exactly (lower case). Anything unrecognised or unavailable resolves to
// gdraw with FellBack set, so the caller can report it; the result never names a
// backend that cannot be constructed.
inline XaraRendererSelection XaraResolveRenderer(const char* pValue, bool CairoAvailable)
{
	XaraRendererSelection Sel = { XARA_RENDERER_GDRAW, XARA_RENDERER_GDRAW, true, false };

	if (pValue == NULL || pValue[0] == '\0' || std::strcmp(pValue, "gdraw") == 0)
		return Sel;

	if (std::strcmp(pValue, "cairo") == 0)
	{
		Sel.Requested = XARA_RENDERER_CAIRO;
		if (CairoAvailable)
			Sel.Effective = XARA_RENDERER_CAIRO;
		else
			Sel.FellBack = true;
		return Sel;
	}

	Sel.Recognised = false;
	Sel.FellBack = true;
	return Sel;
}

// Format the diagnostic for a selection. Returns false (and leaves pBuf empty) when
// nothing should be reported: default startup and an explicit gdraw stay quiet.
inline bool XaraRendererDiagnostic(const XaraRendererSelection& Sel, const char* pValue,
								   char* pBuf, size_t BufLen)
{
	if (BufLen > 0)
		pBuf[0] = '\0';
	if (!Sel.FellBack || pValue == NULL)
		return false;

	if (!Sel.Recognised)
		std::snprintf(pBuf, BufLen, "Xara renderer warning: unknown XARA_RENDERER value '%s' "
					  "(expected gdraw or cairo); falling back to %s.",
					  pValue, XaraRendererName(Sel.Effective));
	else
		std::snprintf(pBuf, BufLen, "Xara renderer: XARA_RENDERER=%s was requested, but the %s "
					  "renderer is not available in this build; falling back to %s.",
					  pValue, XaraRendererName(Sel.Requested), XaraRendererName(Sel.Effective));
	return true;
}

#endif
