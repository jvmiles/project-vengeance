# Redistribution and third-party review

This is a technical license inventory, not a grant of rights or legal clearance.
Original notices remain unmodified. This source-only publication proposal retains the supplied license conditions.

| Component | Terms / required action |
|---|---|
| Xara LX and revival source | GPL version 2, LICENSE additional linking permissions; include GPL, LICENSE, AUTHORS and all source notices |
| CDraw archives (including GDraw entry points) | libs/LIBS-LICENSE permits object-code distribution with a GPLv2 Xara LX derivative executable/package OR a larger work containing complete derivative source; linking/use with other works prohibited; preserve license and archives |
| CDraw GPL source exception | LICENSE explicitly permits satisfying CDraw source provision with binary library plus accompanying license; no permission for public decompiled source is established |
| wxXtra | wxWindows Library Licence 3.1 with LGPL terms and exception; include wxXtra/WXXTRA-LICENSE and COPYING.LIB |
| wxWidgets | wxWindows Library Licence; use distribution dependency, do not bundle without its notices |
| Cairo | LGPL-2.1 / MPL-1.1 alternative licensing; use system library; retain own GPLv2 Cairo-adapter notices |
| GTK, GLib, Pango | LGPL family; system dynamic dependencies, verify exact package copyrights before any binary redistribution |
| FreeType | FreeType License / GPL alternatives; system package license/attribution applies |
| libxml2, libpng, libjpeg | MIT-style, libpng and IJG/BSD-style respective package terms; preserve exact notices if bundled |
| Python, Pillow | PSF and HPND respective terms; external build/runtime dependencies |
| jsonschema / MCP Python SDK | MIT dependencies; optional Python environments not distributed in candidate |
| ImageMagick / Ghostscript | ImageMagick License; Ghostscript AGPL/commercial as applicable, external optional processes; no bundled tools |
| Historical designs and help | Designs/LICENSE and doc/en/LICENSE title says CC BY-NC-ND 2.5 but text permits commercial use and links CC BY-ND 2.5; inconsistent. Omitted from candidate archive pending clarification; unchanged in Git |
| TextDesigns/test fixtures | Individual provenance not comprehensively established. Omitted from public archive pending review; retained privately for tests |
| Project Vengeance logo | Existing approved asset retained exactly; project-use instruction reaffirmed by the user. No redesign. External copyright/trademark clearance is not independently established |
| Xara names/marks | LICENSE and LIBS-LICENSE reserve trademark rights. Software permission does not grant trademark use; no trademark permission is established by these software licenses; this is an unofficial derivative |

CDraw is not licensed GPL and must not be advertised as open-source code. The
included archives have not been modified. Their hashes are in CDRAW_PROVENANCE.json.
Their checked-in license supports conditional redistribution; CDRAW_REDISTRIBUTION.md
and CDRAW_PROVENANCE.json establish the historical archive/license chain and conditions.

Research/decompilation, private operational reports, raw logs, evidence and
recovery bundles are excluded from this allowlisted public snapshot. They remain in
private history. Publishing the repository history would expose them anyway and
requires a separate full-history audit; do not publish this clone or its bundle.

The source candidate retains all compiled application code, binary CDraw inputs,
resources, build scripts and notices. Excluding optional sample/help content is
not a license change. No third-party Python environment or system shared library
is shipped. This inventory is not a complete binary-package SBOM.
