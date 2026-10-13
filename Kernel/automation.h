#ifndef XARA_AUTOMATION_H
#define XARA_AUTOMATION_H
#include <wx/string.h>
#include <vector>
#include <string>

namespace XaraAutomation {
struct Request {
    wxString method;
    int x, y, width, height, x2, y2, strokeWidth, fontSize;
    wxString fill, stroke, text;
    Request() : x(0), y(0), width(0), height(0), x2(0), y2(0), strokeWidth(0), fontSize(24000) {}
};
struct Object {
    unsigned int tag;
    wxString type;
    int x0, y0, x1, y1;
    bool selected;
    wxString fill, stroke, text;
    int strokeWidth, fontSize, anchorX, anchorY, endX, endY;
    Object() : strokeWidth(0), fontSize(0), anchorX(0), anchorY(0), endX(0), endY(0) {}
};
struct Result {
    bool ok;
    wxString error;
    unsigned int createdTag;
    std::vector<Object> objects;
    Result() : ok(false), createdTag(0) {}
};
// Call only on the GUI thread. Transport/provider code must not touch Nodes.
Result Execute(const Request& request);
std::vector<Result> ExecuteBatch(const std::vector<Request>& requests, bool transaction);
// Shared bounded XML transport parser: validates only, never mutates.
bool ParseSceneXML(const std::string&, std::vector<Request>&, bool& transaction);
// Opt-in startup demonstration; no listener and no provider/model execution.
void RunStartupRequest();
}
#endif
