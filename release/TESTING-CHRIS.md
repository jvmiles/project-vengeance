# Linux beta test guide for Chris

Start with Ubuntu 22.04 x86_64 and an X11 desktop, the tested configuration.
Other distributions/wxWidgets versions are not yet certified. Use only disposable
copies of documents. This beta is source-only; avoid system-wide make install.

## Build and launch

Download the named `vengeance-v0.1.0-beta.1-source.tar.gz` release asset,
SOURCE_SHA256SUMS and companion BUILD.md, not GitHub's generated source archive.

```bash
sha256sum -c SOURCE_SHA256SUMS
sudo apt-get install --no-install-recommends build-essential autoconf automake \
 libtool gettext autopoint pkg-config libwxgtk3.0-gtk3-dev libgtk-3-dev \
 libxml2-dev libfreetype-dev libpango1.0-dev libpng-dev libjpeg-dev libcairo2-dev \
 perl zip unzip python3 python3-pil python3-venv
tar -xzf vengeance-v0.1.0-beta.1-source.tar.gz
cd vengeance-v0.1.0-beta.1
./autogen.sh
./configure --disable-international --disable-svnversion --disable-filters --disable-xarlib
make -j2
ldd ./XaraLX
```

If autopoint is missing, see BUILD.md's gettext fallback. Stop if compilation
fails or ldd reports a missing library. Optional ImageMagick/Ghostscript support
for bitmap-PDF exports depends on local policy; it is not vector PDF.

```bash
beta_run=$(mktemp -d /tmp/vengeance-chris.XXXXXX)
mkdir "$beta_run/settings"
printf 'Test evidence: %s\n' "$beta_run"
sha256sum ./XaraLX > "$beta_run/binary.sha256"
set +e
XARA_RENDERER=gdraw XARA_DATA_DIR="$PWD" \
 XARA_CONFIG_DIR="$beta_run/settings" XARA_ASSERT_TRACE=1 G_DEBUG=fatal-criticals \
 ./XaraLX > "$beta_run/startup.log" 2>&1
beta_exit=$?
printf 'Application exit: %s\n' "$beta_exit" > "$beta_run/status.log"
```

Keep your existing DISPLAY/XAUTHORITY; do not relax X server permissions.

## Short manual checklist

1. Draw a rectangle and circle; select/edit each. Try Undo/Redo.
2. Save a fresh XAR inside the printed evidence directory, close/reopen it and
   confirm objects remain editable. Never overwrite an existing user document.
3. Open PNG export options and Cancel; reopen and export a fresh PNG. Open JPEG
   options, check preview and controls; export a fresh JPEG, then reopen options
   and Cancel/window-close. Open each exported image to check content/colours.
4. If practical, test text, grouping, move/resize/rotate and copy/cut/paste.
   These are requests for additional coverage, not claims of previous full PASS.
5. Close normally. Report any critical/assertion, abnormal exit or visual defect.

Default GDraw is the primary test. An optional separate launch with
XARA_RENDERER=cairo and a new evidence directory tests the experimental hybrid
backend; record renderer per action, without assuming parity. SVG and vector PDF
are not supported; bitmap PDF is optional and should be identified as raster.
For a separate optional Cairo run, use the launch command above with
XARA_RENDERER=cairo and a new evidence directory. The Cairo startup diagnostic
should report its hybrid backend; unsupported operations still use GDraw.

## Optional local Ollama test

With Ollama already installed/running, obtain the model separately:

```bash
ollama pull qwen2.5-coder:7b
ollama list
python3 -m venv .vengeance-venv
.vengeance-venv/bin/python -m pip install 'jsonschema==4.26.0'
ai_run=$(mktemp -d /tmp/vengeance-chris-ollama.XXXXXX)
mkdir "$ai_run/settings"
XARA_RENDERER=gdraw XARA_AI_ASSISTANT=1 XARA_ASSISTANT_ROOT="$PWD" \
 XARA_ASSISTANT_PYTHON="$PWD/.vengeance-venv/bin/python" \
 XARA_DATA_DIR="$PWD" XARA_CONFIG_DIR="$ai_run/settings" \
 ./XaraLX > "$ai_run/startup.log" 2>&1
```

In the Assistant select Ollama/local, set endpoint http://127.0.0.1:11434,
model qwen2.5-coder:7b, API key blank. In a disposable populated document, replace
the demo prompt with “Draw a red circle and a blue rectangle next to it.” Click
Generate, wait for a validated response, then Apply. Confirm editable native
objects, colours and placement; one Undo should remove only the generated scene,
and Redo restore it. Submit again and Cancel while pending; confirm no insertion
and responsive GUI. A demo.scene result is not real inference. Report generation
errors honestly; this guide does not itself record HTTP evidence or certify
inference. Do not share credentials or unredacted provider logs.

Send the distro/desktop, compiler/wxWidgets versions, binary hash, renderer,
action-by-action PASS/FAIL/NOT TESTED, startup/status logs and small disposable
XAR/export/screenshot examples. Review logs/images for personal information and
credentials before sharing. A warning alone is not proof of functional failure;
critical/assertion/crash evidence is a failure and should stop that test.
