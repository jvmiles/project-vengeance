// SPDX-License-Identifier: GPL-2.0-only
// Single construction point for GDrawContext implementations.

#include "camtypes.h"
// camconfig.h must be included immediately after camtypes.h
#include "camconfig.h"

#include <cstdio>
#include <cstdlib>

#include "gdrawfactory.h"
#include "GDrawIntf.h"
#include "gdrawcairo.h"

const XaraRendererSelection& GDrawFactory::GetSelection()
{
	static bool s_bResolved = false;
	static XaraRendererSelection s_Selection;

	if (!s_bResolved)
	{
		const char* pValue = getenv("XARA_RENDERER");
		s_Selection = XaraResolveRenderer(pValue, IsCairoAvailable());
		s_bResolved = true;

		// Default and explicit gdraw stay quiet; a fallback or the partial cairo backend
		// is always reported.
		char Message[256];
		if (XaraRendererDiagnostic(s_Selection, pValue, Message, sizeof(Message)))
			fprintf(stderr, "%s\n", Message);
		else if (s_Selection.Effective == XARA_RENDERER_CAIRO)
			fprintf(stderr, "Xara renderer: using cairo for solid-colour fills, strokes, rectangles and "
							"points with rectangular clipping on 32bpp and transparency-channel bitmaps "
							"(Stage 2B); every other operation is drawn with gdraw.\n");
	}

	return s_Selection;
}

XaraRendererKind GDrawFactory::GetRenderer()
{
	return GetSelection().Effective;
}

GDrawContext* GDrawFactory::Create()
{
	switch (GetRenderer())
	{
	case XARA_RENDERER_GDRAW:
		return new GDrawAsm;

	case XARA_RENDERER_CAIRO:
		return new GDrawCairo;
	}

	ERROR3("GDrawFactory::Create - selected renderer cannot be constructed");
	return NULL;
}
