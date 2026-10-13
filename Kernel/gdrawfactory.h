// SPDX-License-Identifier: GPL-2.0-only
// Single construction point for GDrawContext implementations.
#ifndef XARA_GDRAW_FACTORY_H
#define XARA_GDRAW_FACTORY_H

#include "gdrawcon.h"
#include "rendererselect.h"

/*******************************************************************************************

>	class GDrawFactory

	Purpose:	Creates GDrawContext objects for the renderer selected by the XARA_RENDERER
				environment variable (gdraw|cairo). gdraw (GDrawAsm over CDraw) is the
				default. Selection is resolved once per process; an unrecognised or
				unavailable choice falls back to gdraw with one explicit message on stderr.

				Create() only constructs the object. Callers still call Init() exactly as
				they did with "new GDrawAsm", so initialisation order is unchanged.

********************************************************************************************/

class GDrawFactory
{
public:
	static GDrawContext* Create();
	static XaraRendererKind GetRenderer();
	static const XaraRendererSelection& GetSelection();

	// GDrawCairo (Stage 2A subset; everything else is drawn by its internal GDraw context).
	static bool IsCairoAvailable() { return true; }
};

#endif
