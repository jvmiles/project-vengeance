// SPDX-License-Identifier: GPL-2.0-only
#include "assistantpreset.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main() {
    std::vector<XaraAutomation::Request> r;
    CHECK(XaraAssistant::Preset(wxT("demo.scene"),r));CHECK(r.size()==6);
    CHECK(r[0].method==wxT("document.list_objects") && r[5].method==r[0].method);
    CHECK(r[1].method==wxT("object.create_rectangle") && r[1].x==72000 && r[1].y==180000 && r[1].width==180000 && r[1].height==108000 && r[1].fill==wxT("#185ADB") && r[1].stroke==wxT("none"));
    CHECK(r[2].method==wxT("object.create_ellipse") && r[2].x==288000 && r[2].y==180000 && r[2].width==108000 && r[2].height==108000 && r[2].fill==wxT("#D4AF37") && r[2].stroke==wxT("none"));
    CHECK(r[3].method==wxT("object.create_line") && r[3].x==72000 && r[3].y==144000 && r[3].x2==396000 && r[3].y2==108000 && r[3].stroke==wxT("#E53935") && r[3].strokeWidth==3000 && r[3].fill.empty());
    CHECK(r[4].method==wxT("object.create_text") && r[4].x==72000 && r[4].y==54000 && r[4].fontSize==24000 && r[4].text==wxT("PROJECT VENGEANCE") && r[4].fill==wxT("#202020") && r[4].stroke==wxT("none"));
    CHECK(XaraAssistant::Preset(wxT(" \ndemo.circle\n"),r) && r.size()==3 && r[1].method==wxT("object.create_ellipse"));
    CHECK(XaraAssistant::Preset(wxT("demo.rectangle"),r) && r.size()==3 && r[1].method==wxT("object.create_rectangle"));
    CHECK(!XaraAssistant::Preset(wxT("Draw me a circle"),r) && r.empty());
    CHECK(!XaraAssistant::Preset(wxT("demo.scene\nrun command"),r) && r.empty());
    CHECK(!XaraAssistant::Preset(wxT(""),r) && r.empty());
    std::puts("PASS: presets match API 0.2 fixture, unsupported input rejected");return 0;
}
