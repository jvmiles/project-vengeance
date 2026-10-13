# Project Vengeance v0.1.0-beta.1

**Xara Xtreme is back. After 20 years. With a vengeance.**

A source-only Linux beta of the revived Xara Xtreme LX editor, preserving the
original interface, editable vector objects and CDraw 4.4 rendering integration.
Original copyright, author credits and license notices remain in place.
Improvements may be contributed upstream subject to upstream requirements.

GDraw is the default renderer. The Cairo hybrid backend and local Ollama AI
Assistant are experimental. Earlier integrated tests verified native AI-generated
objects, shared Undo/Redo and real local Ollama inference. These earlier results
are separate from the focused final test of the newly clean-built executable.

## Release checks

The exact frozen source archive built successfully on Ubuntu 22.04 with GCC
11.4.0, wxWidgets 3.0.5, GTK 3.24.33 and Cairo 1.16.0. Final GDraw GUI acceptance
confirmed PNG options opening/cancellation, JPEG preview/controls and normal
shutdown. Manual checks are user-attested; binary/source identity, exit zero and
absence of GTK critical/assertion/crash markers were machine-verified. Earlier
XAR fixture/roundtrip, JPEG and raster-PDF export reports are retained without
assuming every operation passed under both renderers.

## Known limitations

- Cairo is hybrid and experimental; unsupported operations still use GDraw.
  Final PNG/JPEG GUI parity with Cairo is not established.
- PDF export is bitmap-based; SVG and native vector PDF are unsupported in the
  tested configuration. Localization, SVG filters and XarLib are disabled.
- Ordinary GTK theme warnings and pre-display coordinate debug messages remain.
- General editing/clipboard/font/group/transform and renderer comparisons have
  incomplete coverage. This beta needs additional Linux testing.
- The AI Assistant requires separate local/provider configuration. Credentials
  must stay out of source and logs. A full AI retest on this new binary was not run.
- There is no validated portable binary package, system installation procedure or
  bit-for-bit reproducible build claim. Build/run from the extracted source tree.

CDraw is proprietary and conditionally redistributable with the complete GPLv2
Xara derivative source/package and supplied notices. Read LICENSE, GPL and
libs/LIBS-LICENSE plus release/CDRAW_REDISTRIBUTION.md; it is not a standalone
open-source library or a license to redistribute decompiled proprietary source.

## Source and public package identity

Native source, libraries, resources, build scripts and tests match frozen commit
`1e00daef7996c88c5b003d125a2427052bb9bf0c`. The original frozen archive was built
successfully and its binary passed focused GDraw acceptance. This public snapshot
only updates README/release documentation and excludes private release reports
and host harnesses. Its package checksum differs; obtain it from SOURCE_SHA256SUMS.
No additional clean build was run for these documentation-only packaging changes.

Use the named `vengeance-v0.1.0-beta.1-source.tar.gz` release asset, companion
BUILD.md, TESTING-CHRIS.md and build-report.json. The public tag will identify a
new curated snapshot commit, not the private frozen-source commit. Original
history remains preserved privately. GitHub's generated source archives have
different archive hashes; use the named checksum-verified release asset.
