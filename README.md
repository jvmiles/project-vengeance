# Project Vengeance

**Xara Xtreme is back. After 20 years. With a vengeance.**

Project Vengeance revives Xara Xtreme LX as a native Linux vector graphics editor,
preserving its original interface, native editable objects and CDraw 4.4 engine.
The proposed first release is **v0.1.0-beta.1**, a source-only beta.

- [Build and run on Linux](release/BUILD.md)
- [Release notes and known limitations](release/RELEASE_NOTES.md)
- [Linux testing guide for Chris](release/TESTING-CHRIS.md)
- [Release-readiness summary](release/READINESS.md)
- [Third-party licenses](release/THIRD_PARTY.md)
- [CDraw redistribution conditions](release/CDRAW_REDISTRIBUTION.md)

GDraw is the default renderer. Cairo is an experimental hybrid backend that
falls back to GDraw. The optional local Ollama AI Assistant creates validated,
native editable objects with explicit Apply and Undo/Redo. Neither experimental
feature implies full renderer parity or unrestricted AI control.

The tested target is Ubuntu 22.04/Bodhi x86_64, wxGTK 3.0.5 and X11. Use disposable
documents, save often, and read the limitations. PDF export is raster; SVG and
native vector PDF are unsupported in the tested configuration. No portable binary
package, system installation or bit-for-bit build reproducibility is claimed.

## Credit and licensing

This is an independent derivative, not an official Xara product. Original Xara
Group copyrights and contributor notices are retained; see AUTHORS and source
headers. Historical upstream: https://github.com/samuell/xara-xtreme.
Improvements may be contributed upstream subject to upstream requirements.

Application source is GPLv2 with the additional permissions in LICENSE. CDraw
is proprietary and conditionally redistributable with the complete GPLv2 Xara
derivative source/package under libs/LIBS-LICENSE, not as a standalone library.
This distribution is not uniformly open source. All original license texts,
approved logo bytes and attribution are preserved. Software licenses do not grant
trademark rights; artwork/trademark clearance is not independently certified.

## Source identity

Native code, libraries, resources, build scripts and tests match frozen source
commit `1e00daef7996c88c5b003d125a2427052bb9bf0c`. Only README/release documentation
and private release-harness/report exclusions differ in this public packaging.
The exact frozen-source build passed focused GDraw GUI acceptance. A new public
package has a separate checksum; the older private frozen tarball is not uploaded.
This repository has a fresh public snapshot history, with original history safely
preserved privately. It does not contain private diagnostics or recovery bundles.
