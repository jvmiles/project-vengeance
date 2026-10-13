# Build and run on Ubuntu 22.04

The integrated native source previously built successfully with GCC 11 and
wxGTK 3.0.5. The candidate-specific clean-checkout verification is recorded in
FROZEN_BUILD_RESULT.json, with final focused GDraw GUI acceptance recorded
in the companion release notes/build report. This does not establish byte-for-byte reproducible binaries;
compiler, timestamps, resource tools and Pillow versions affect output.

Install dependencies explicitly on your host (no installer runs automatically):

```bash
sudo apt-get install --no-install-recommends build-essential autoconf automake \
 libtool gettext autopoint pkg-config libwxgtk3.0-gtk3-dev libgtk-3-dev \
 libxml2-dev libfreetype-dev libpango1.0-dev libpng-dev libjpeg-dev libcairo2-dev \
 perl zip unzip python3 python3-pil python3-venv
```

Optional: ImageMagick for bitmap-based PDF/other exports; its PDF policy and
Ghostscript availability vary. Xvfb/xauth are optional headless-test dependencies.
No Wine, proprietary driver changes or replacement CDraw installation is needed.

Build in a new extracted source directory, never over a protected installation:

```bash
wx-config --version
pkg-config --modversion gtk+-3.0 cairo freetype2 pangofc libpng libjpeg
# If autopoint is unavailable (localization stays disabled):
# cp /usr/share/gettext/config.rpath .
# cp /usr/share/gettext/po/Makefile.in.in po/
# export AUTOPOINT=true
./autogen.sh
./configure --disable-international --disable-svnversion --disable-filters --disable-xarlib
make -j2
ldd ./XaraLX
```

These flags match the verified integrated build. Cairo headers/link libraries
come through GTK3 pkg-config metadata; libcairo2-dev is explicitly listed.
SVGFilter, XarLib and localization are disabled. Do not assume other flags work.
Compilation is a legacy in-source build. Do not run make install system-wide:
packaging/install rules have not been validated for this beta.

Run from that source directory (retain it alongside the executable):

```bash
mkdir -p "$PWD/.vengeance-test-settings"
XARA_DATA_DIR="$PWD" XARA_CONFIG_DIR="$PWD/.vengeance-test-settings" \
 XARA_RENDERER=gdraw ./XaraLX
# Experimental hybrid backend:
XARA_DATA_DIR="$PWD" XARA_CONFIG_DIR="$PWD/.vengeance-test-settings" \
 XARA_RENDERER=cairo ./XaraLX
```

Use your existing X11 DISPLAY/XAUTHORITY. Do not change X server permissions.
Open XAR files through File > Open; command-line document loading is not certified.

## Experimental local AI Assistant

```bash
python3 -m venv .vengeance-venv
.vengeance-venv/bin/python -m pip install 'jsonschema==4.26.0'
XARA_AI_ASSISTANT=1 XARA_ASSISTANT_ROOT="$PWD" \
 XARA_ASSISTANT_PYTHON="$PWD/.vengeance-venv/bin/python" \
 XARA_DATA_DIR="$PWD" XARA_CONFIG_DIR="$PWD/.vengeance-test-settings" ./XaraLX
```

Start Ollama separately; the host-verified model is qwen2.5-coder:7b. In the panel,
select Ollama/local, Settings endpoint http://127.0.0.1:11434, model
qwen2.5-coder:7b, key blank. Replace the demo prompt with natural language.
Generate asynchronously, review the validated scene, then Apply. Demo commands
are not inference. Test Undo/Redo and cancellation in a disposable document.
Credentials are session-only; never commit keys or model/provider transcripts.
OpenAI-compatible adapter exists but is not host-accepted; Anthropic is future work.

MCP remains optional local stdio, with dependencies in
`automation/mcp/requirements.txt`. It launches a separate editor session; it is
not a persistent connection to the open document. No network listener is enabled.
