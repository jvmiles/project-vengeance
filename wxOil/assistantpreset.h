// SPDX-License-Identifier: GPL-2.0-only
#ifndef XARA_ASSISTANT_PRESET_H
#define XARA_ASSISTANT_PRESET_H
#include "automation.h"
namespace XaraAssistant {
// Explicit presets only. No natural-language parser, file access or executable code.
inline bool Preset(const wxString& input, std::vector<XaraAutomation::Request>& out) {
    out.clear();
    wxString name=input; name.Trim(true).Trim(false);
    if(name!=wxT("demo.scene") && name!=wxT("demo.circle") && name!=wxT("demo.rectangle"))return false;
    XaraAutomation::Request list;list.method=wxT("document.list_objects");out.push_back(list);
    XaraAutomation::Request r;r.method=wxT("object.create_rectangle");r.x=72000;r.y=180000;
    r.width=180000;r.height=108000;r.fill=wxT("#185ADB");r.stroke=wxT("none");
    if(name!=wxT("demo.circle"))out.push_back(r);
    r.method=wxT("object.create_ellipse");r.x=288000;r.width=108000;r.fill=wxT("#D4AF37");
    if(name!=wxT("demo.rectangle"))out.push_back(r);
    if(name==wxT("demo.scene")) {
        r=XaraAutomation::Request();r.method=wxT("object.create_line");r.x=72000;r.y=144000;
        r.x2=396000;r.y2=108000;r.stroke=wxT("#E53935");r.strokeWidth=3000;out.push_back(r);
        r=XaraAutomation::Request();r.method=wxT("object.create_text");r.x=72000;r.y=54000;
        r.text=wxT("PROJECT VENGEANCE");r.fontSize=24000;r.fill=wxT("#202020");r.stroke=wxT("none");out.push_back(r);
    }
    out.push_back(list);return true;
}
}
#endif
