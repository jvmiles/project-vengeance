// Renderer-independent automation. All attached edits use native undo operations.
#include "camtypes.h"
#include "automation.h"
#include "document.h"
#include "spread.h"
#include "layer.h"
#include "noderect.h"
#include "nodeelip.h"
#include "nodepath.h"
#include "nodetxts.h"
#include "nodetext.h"
#include "txtattr.h"
#include "fillattr.h"
#include "lineattr.h"
#include "zoomops.h"
#include "selop.h"
#include "attrmgr.h"
#include <wx/thread.h>
#include <wx/dialog.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <algorithm>

namespace XaraAutomation {
static bool IsCreate(const wxString& method) {
    return method == wxT("object.create_rectangle") || method == wxT("object.create_ellipse") ||
        method == wxT("object.create_line") || method == wxT("object.create_text");
}
static bool ColourValid(const wxString& value) {
    if (value.empty() || value == wxT("none")) return true;
    if (value.length() != 7 || value[0] != '#') return false;
    for (size_t n=1; n<7; ++n)
        if (!((value[n]>='0' && value[n]<='9') || (value[n]>='a' && value[n]<='f') || (value[n]>='A' && value[n]<='F'))) return false;
    return true;
}
static DocColour Colour(const wxString& value) {
    if (value == wxT("none")) return DocColour(COLOUR_TRANS);
    long rgb=0; value.Mid(1).ToLong(&rgb,16);
    return DocColour((INT32)((rgb>>16)&255),(INT32)((rgb>>8)&255),(INT32)(rgb&255));
}
static wxString ColourString(DocColour* colour) {
    if (!colour) return wxString();
    if (colour->IsTransparent()) return wxT("none");
    INT32 r,g,b; colour->GetRGBValue(&r,&g,&b);
    return wxString::Format(wxT("#%02X%02X%02X"),(unsigned)r,(unsigned)g,(unsigned)b);
}
static wxString Validate(const Request& r) {
    if (r.method == wxT("document.list_objects")) return wxString();
    if (!IsCreate(r.method)) return wxT("unsupported_method");
    const long long limit=10000000;
    if (r.x < -limit || r.x > limit || r.y < -limit || r.y > limit ||
        !ColourValid(r.fill) || !ColourValid(r.stroke) || r.strokeWidth < 0 || r.strokeWidth > 1000000)
        return wxT("invalid_parameters");
    if (r.method == wxT("object.create_line")) {
        if (r.x2 < -limit || r.x2 > limit || r.y2 < -limit || r.y2 > limit ||
            (r.x==r.x2 && r.y==r.y2) || !r.fill.empty()) return wxT("invalid_line");
    } else if (r.method == wxT("object.create_text")) {
        if (r.text.empty() || r.text.length()>256 || r.fontSize<1000 || r.fontSize>1000000) return wxT("invalid_text");
        for (size_t n=0;n<r.text.length();++n) if (r.text[n]<32 || r.text[n]>126) return wxT("unsupported_text_encoding");
    } else if (r.width<=0 || r.height<=0 || r.width>limit || r.height>limit ||
        (long long)r.x+r.width>limit || (long long)r.y+r.height>limit) return wxT("invalid_bounds");
    return wxString();
}
static wxString ContextError() {
    if (!wxIsMainThread()) return wxT("gui_thread_required");
    for (wxWindowList::compatibility_iterator i=wxTopLevelWindows.GetFirst();i;i=i->GetNext()) {
        wxDialog* d=wxDynamicCast(i->GetData(),wxDialog);
        if (d && d->IsModal()) return wxT("editor_busy");
    }
    Document* d=Document::GetSelected();
    if (!d) return wxT("no_document");
    if (Operation::GetCurrentDragOp() || Document::GetCurrent()!=d) return wxT("editor_busy");
    return wxString();
}
static Object Snapshot(NodeRenderableInk* node) {
    Object o; o.tag=node->GetTag();o.type=node->GetRuntimeClass()->GetClassName();o.selected=node->IsSelected();
    DocRect b=node->GetBoundingRect(TRUE);o.x0=b.lo.x;o.y0=b.lo.y;o.x1=b.hi.x;o.y1=b.hi.y;
    NodeRenderableInk* styleNode=node;
    if(node->IsKindOf(CC_RUNTIME_CLASS(TextStory))) {
        for(Node* ch=node->FindFirstDepthFirst();ch;ch=ch->FindNextDepthFirst(node))
            if(ch->IsKindOf(CC_RUNTIME_CLASS(TextChar))){styleNode=(NodeRenderableInk*)ch;break;}
    }
    NodeAttribute* a=styleNode->FindAppliedAttribute(CC_RUNTIME_CLASS(AttrFillGeometry));
    if (a && a->IsKindOf(CC_RUNTIME_CLASS(AttrFlatColourFill))) o.fill=ColourString(((AttrFillGeometry*)a)->GetStartColour());
    a=styleNode->FindAppliedAttribute(CC_RUNTIME_CLASS(AttrStrokeColour));
    if (a) o.stroke=ColourString(&((AttrStrokeColour*)a)->Value.Colour);
    a=styleNode->FindAppliedAttribute(CC_RUNTIME_CLASS(AttrLineWidth));
    if (a) o.strokeWidth=((AttrLineWidth*)a)->Value.LineWidth;
    if (node->IsKindOf(CC_RUNTIME_CLASS(TextStory))) {
        TextStory* story=(TextStory*)node;DocCoord anchor;story->GetStoryMatrix().GetTranslation(anchor);o.anchorX=anchor.x;o.anchorY=anchor.y;
        a=styleNode->FindAppliedAttribute(CC_RUNTIME_CLASS(AttrTxtFontSize));if(a)o.fontSize=((AttrTxtFontSize*)a)->Value.FontSize;
        for(Node* ch=story->FindFirstDepthFirst();ch && o.text.length()<256;ch=ch->FindNextDepthFirst(story))
            if(ch->IsKindOf(CC_RUNTIME_CLASS(TextChar)))o.text+=(wxChar)((TextChar*)ch)->GetUnicodeValue();
    } else if(node->IsKindOf(CC_RUNTIME_CLASS(NodePath))) {
        Path& path=((NodePath*)node)->InkPath;
        if(path.GetNumCoords()==2) {DocCoord* c=path.GetCoordArray();o.anchorX=c[0].x;o.anchorY=c[0].y;o.endX=c[1].x;o.endY=c[1].y;}
    }
    return o;
}
static bool Replace(NodeRenderableInk* node, NodeAttribute* attr, CCRuntimeClass* type) {
    if(!attr)return false;
    NodeAttribute* old=node->GetChildAttrOfType(type);
    if(old){old->CascadeDelete();delete old;}
    attr->AttachNode(node,FIRSTCHILD);return true;
}
static bool Style(NodeRenderableInk* node,const Request& r) {
    // Detached node: its entire attribute subtree is owned by the insertion undo action.
    if(!r.fill.empty()) {
        AttrFlatColourFill* a=new AttrFlatColourFill;if(!a)return false;DocColour c=Colour(r.fill);a->SetStartColour(&c);
        if(!Replace(node,a,CC_RUNTIME_CLASS(AttrFillGeometry)))return false;
        AttrFlatTranspFill* t=new AttrFlatTranspFill;if(!t)return false;UINT32 zero=0;t->SetStartTransp(&zero);
        if(!Replace(node,t,CC_RUNTIME_CLASS(AttrTranspFillGeometry)))return false;
    }
    if(!r.stroke.empty()) {
        AttrStrokeColour* a=new AttrStrokeColour;if(!a)return false;a->Value.Colour=Colour(r.stroke);
        if(!Replace(node,a,CC_RUNTIME_CLASS(AttrStrokeColour)))return false;
        AttrStrokeTransp* t=new AttrStrokeTransp;if(!t)return false;t->Value.Transp=0;t->Value.TranspType=TT_Mix;
        if(!Replace(node,t,CC_RUNTIME_CLASS(AttrStrokeTransp)))return false;
    }
    if(r.strokeWidth) {
        AttrLineWidth* a=new AttrLineWidth;if(!a)return false;a->Value.LineWidth=r.strokeWidth;
        if(!Replace(node,a,CC_RUNTIME_CLASS(AttrLineWidth)))return false;
    }
    if(r.method==wxT("object.create_text")) {
        AttrTxtFontSize* a=new AttrTxtFontSize;if(!a)return false;a->Value.FontSize=r.fontSize;
        if(!Replace(node,a,CC_RUNTIME_CLASS(AttrTxtFontSize)))return false;
    }
    return true;
}
static NodeRenderableInk* Build(const Request& r) {
    NodeRenderableInk* node=NULL;
    if(r.method==wxT("object.create_text")) {
        wxCharBuffer text=r.text.utf8_str();
        node=TextStory::CreateFromChars(DocCoord(r.x,r.y),const_cast<char*>(text.data()),NULL,Document::GetSelected());
    } else {
        NodePath* path=NULL;
        if(r.method==wxT("object.create_line")) {
            path=new NodePath;
            if(path && (!path->SetUpPath() || !path->InkPath.InsertMoveTo(DocCoord(r.x,r.y)) ||
                !path->InkPath.InsertLineTo(DocCoord(r.x2,r.y2)))) {delete path;return NULL;}
            if(path)path->InkPath.IsFilled=FALSE;
        } else {
            NodeSimpleShape* shape=r.method==wxT("object.create_ellipse") ? static_cast<NodeSimpleShape*>(new NodeEllipse) : static_cast<NodeSimpleShape*>(new NodeRect);
            if(shape && !shape->SetUpPath()){delete shape;return NULL;}
            if(shape){shape->CreateShape(DocRect(r.x,r.y,r.x+r.width,r.y+r.height));shape->InkPath.IsFilled=TRUE;}
            node=shape;
        }
        if(path)node=path;
        if(node && !Document::GetSelected()->GetAttributeMgr().ApplyCurrentAttribsToNode(node)){node->CascadeDelete();delete node;return NULL;}
    }
    if(node && !Style(node,r)) {
        node->CascadeDelete();delete node;return NULL;
    }
    return node;
}
}

class OpAutomationScene : public SelOperation {
    CC_DECLARE_DYNCREATE(OpAutomationScene);
public:
    void Do(OpDescriptor*) {End();}
    void GetOpName(String_256* name){*name=_T("Automation: create objects");}
    void Apply(const std::vector<XaraAutomation::Request>& requests,std::vector<XaraAutomation::Result>& results) {
        results.resize(requests.size());
        if(!DoStartSelOp(FALSE) || !UndoActions.GetHead() || OpFlags.SucceedAndDiscard){Fail(results);return;}
        for(size_t n=0;n<requests.size();++n) {
            NodeRenderableInk* node=XaraAutomation::Build(requests[n]);
            if(!node){Fail(results);return;}
            size_t hidesBefore=HideCount();
            if(!DoInsertNewNode(node,Document::GetSelectedSpread(),TRUE)) {
                if(HideCount()==hidesBefore){
                    if(node->FindParent())node->UnlinkNodeFromTree();
                    node->CascadeDelete();delete node;
                }
                Fail(results);return;
            }
            // Each inserted node must have its own recorded hide action.
            const bool insertionRecorded=HideCount()>hidesBefore;
            if(!insertionRecorded || OpFlags.SucceedAndDiscard){
                if(!insertionRecorded){node->UnlinkNodeFromTree();node->CascadeDelete();delete node;}
                Fail(results);return;
            }
            if(node->IsKindOf(CC_RUNTIME_CLASS(TextStory)) &&
                (!((TextStory*)node)->FormatAndChildren(NULL,FALSE,FALSE) || !DoInvalidateNodeRegion(node,TRUE))) {
                Fail(results);return;
            }
            results[n].createdTag=node->GetTag();results[n].objects.push_back(XaraAutomation::Snapshot(node));
        }
        if(!DoEndSelOp() || OpFlags.SucceedAndDiscard || OpHasFailed()){Fail(results);return;}
        for(size_t n=0;n<results.size();++n)results[n].ok=true;
        Operation::End();
    }
private:
    size_t HideCount() {
        size_t count=0;
        for(Action* a=(Action*)UndoActions.GetHead();a;a=(Action*)UndoActions.GetNext(a))
            if(a->IsKindOf(CC_RUNTIME_CLASS(HideNodeAction)))++count;
        return count;
    }
    void Fail(std::vector<XaraAutomation::Result>& results) {
        for(size_t n=0;n<results.size();++n){results[n].ok=false;results[n].createdTag=0;results[n].objects.clear();results[n].error=wxT("transaction_failed");}
        FailAndExecute();Operation::End();
    }
};
CC_IMPLEMENT_DYNCREATE(OpAutomationScene,SelOperation)

namespace XaraAutomation {
Result Execute(const Request& request) {
    Result result;result.error=Validate(request);if(!result.error.empty())return result;
    result.error=ContextError();if(!result.error.empty())return result;
    if(request.method==wxT("document.list_objects")) {
        unsigned visited=0;
        for(Node* n=Node::DocFindFirstDepthFirst(Document::GetSelected());n;n=n->DocFindNextDepthFirst()) {
            if(++visited>100000){result.error=wxT("tree_limit");result.objects.clear();return result;}
            if(n->IsAnObject() && !n->IsNodeHidden() && n->IsKindOf(CC_RUNTIME_CLASS(NodeRenderableInk)) &&
                !n->FindParent(CC_RUNTIME_CLASS(TextStory))) {
                if(result.objects.size()>=1000){result.error=wxT("object_limit");result.objects.clear();return result;}
                result.objects.push_back(Snapshot((NodeRenderableInk*)n));
            }
        }
        result.ok=true;return result;
    }
    Spread* spread=Document::GetSelectedSpread();Layer* layer=spread?spread->FindActiveLayer():NULL;
    if(!layer || layer->IsLocked() || !layer->IsVisible()){result.error=wxT("layer_not_editable");return result;}
    std::vector<Request> requests(1,request);std::vector<Result> results;
    OpAutomationScene* op=new OpAutomationScene;if(!op){result.error=wxT("allocation_failed");return result;}
    op->Apply(requests,results);return results[0];
}
std::vector<Result> ExecuteBatch(const std::vector<Request>& requests,bool transaction) {
    std::vector<Result> results;
    // Validate every command before any edit, including direct C++ callers.
    wxString error;
    if(requests.empty() || requests.size()>16)error=wxT("invalid_batch");
    for(size_t n=0;n<requests.size() && error.empty();++n)error=Validate(requests[n]);
    if(error.empty())error=ContextError();
    size_t first=0,last=requests.size();
    if(transaction && error.empty()) {
        if(requests.front().method==wxT("document.list_objects"))first=1;
        if(last>first && requests.back().method==wxT("document.list_objects"))--last;
        if(first==last)error=wxT("transaction_requires_creations");
        for(size_t n=first;n<last;++n)if(!IsCreate(requests[n].method))error=wxT("invalid_transaction_order");
        Spread* spread=Document::GetSelectedSpread();Layer* layer=spread?spread->FindActiveLayer():NULL;
        if(!layer || layer->IsLocked() || !layer->IsVisible())error=wxT("layer_not_editable");
    }
    if(!error.empty()){Result r;r.error=error;results.push_back(r);return results;}
    if(!transaction) {
        for(size_t n=0;n<requests.size();++n){results.push_back(Execute(requests[n]));if(!results.back().ok)break;}
        return results;
    }
    if(first){results.push_back(Execute(requests.front()));if(!results.back().ok)return results;}
    std::vector<Request> creations(requests.begin()+first,requests.begin()+last);std::vector<Result> edits;
    OpAutomationScene* op=new OpAutomationScene;
    if(!op){Result r;r.error=wxT("allocation_failed");results.push_back(r);return results;}
    op->Apply(creations,edits);results.insert(results.end(),edits.begin(),edits.end());
    if(!edits.empty() && edits.back().ok && last<requests.size())results.push_back(Execute(requests.back()));
    return results;
}

static wxString Attribute(xmlNode* node, const char* name) {
    xmlChar* text = xmlGetProp(node, BAD_CAST name);
    if (!text) return wxString();
    wxString value = wxString::FromUTF8((const char*)text); xmlFree(text); return value;
}
static bool Integer(const wxString& text, int& value) {
    if (text.empty() || text.length() > 12) return false;
    for (size_t i = 0; i < text.length(); ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (i == 0 && text[i] == '-'))) return false;
    long parsed;
    if (!text.ToLong(&parsed) || parsed < -10000000 || parsed > 10000000) return false;
    value = (int)parsed; return true;
}
static void Property(xmlNode* node, const char* name, const wxString& value) {
    wxCharBuffer utf8 = value.utf8_str(); xmlNewProp(node, BAD_CAST name, BAD_CAST utf8.data());
}
static void Number(xmlNode* node, const char* name, long value) {
    Property(node, name, wxString::Format(wxT("%ld"), value));
}

bool ParseSceneXML(const std::string& bytes,std::vector<Request>& requests,bool& transaction) {
    requests.clear();
    if(bytes.empty() || bytes.size()>65536)return false;
    bool ascii = true;
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if ((ch < 32 && ch != '\t' && ch != '\r' && ch != '\n') || ch > 126) ascii = false;
    }
    xmlDoc* doc = ascii && bytes.find("<!DOCTYPE") == std::string::npos && bytes.find('&') == std::string::npos ?
        xmlReadMemory(bytes.data(), (int)bytes.size(), "request.xml", NULL,
                      XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING) : NULL;
    bool valid = doc && !doc->intSubset && !doc->extSubset;
    xmlNode* root = valid ? xmlDocGetRootElement(doc) : NULL;
    valid = root && !root->ns && xmlStrEqual(root->name, BAD_CAST "requests");
    transaction=false;
    if(valid && root->properties) {
        valid=!root->properties->next && !root->properties->ns &&
            xmlStrEqual(root->properties->name,BAD_CAST "transaction") && Attribute(root,"transaction")==wxT("true");
        transaction=valid;
    }
    for (xmlNode* node = valid ? root->children : NULL; node && valid; node = node->next) {
        valid = node->type == XML_ELEMENT_NODE && !node->ns && !node->children &&
            xmlStrEqual(node->name, BAD_CAST "request") && requests.size() < 16;
        if (!valid) break;
        Request request; request.method = Attribute(node, "method");
        const bool shape=request.method==wxT("object.create_rectangle") || request.method==wxT("object.create_ellipse");
        const bool line=request.method==wxT("object.create_line");
        const bool text=request.method==wxT("object.create_text");
        for (xmlAttr* prop=node->properties;prop && valid;prop=prop->next) {
            std::string name((const char*)prop->name);
            valid=!prop->ns && (name=="method" ||
                (IsCreate(request.method) && (name=="x" || name=="y" || name=="stroke" || name=="stroke_width")) ||
                ((shape || text) && name=="fill") || (shape && (name=="width" || name=="height")) ||
                (line && (name=="x2" || name=="y2")) || (text && (name=="text_hex" || name=="font_size")));
        }
        if(IsCreate(request.method)) {
            valid=valid && Integer(Attribute(node,"x"),request.x) && Integer(Attribute(node,"y"),request.y);
            request.fill=Attribute(node,"fill");request.stroke=Attribute(node,"stroke");
            if(xmlHasProp(node,BAD_CAST "fill"))valid=valid && !request.fill.empty();
            if(xmlHasProp(node,BAD_CAST "stroke"))valid=valid && !request.stroke.empty();
            if(xmlHasProp(node,BAD_CAST "stroke_width"))valid=valid && Integer(Attribute(node,"stroke_width"),request.strokeWidth) && request.strokeWidth>0;
            if(shape)valid=valid && Integer(Attribute(node,"width"),request.width) && Integer(Attribute(node,"height"),request.height);
            if(line)valid=valid && Integer(Attribute(node,"x2"),request.x2) && Integer(Attribute(node,"y2"),request.y2);
            if(text) {
                valid=valid && Integer(Attribute(node,"font_size"),request.fontSize);
                wxString hex=Attribute(node,"text_hex");valid=valid && !hex.empty() && hex.length()<=512 && hex.length()%2==0;
                for(size_t k=0;valid && k<hex.length();k+=2) {
                    long value=0;wxString pair=hex.Mid(k,2);
                    for(size_t j=0;j<2;++j)if(!((pair[j]>='0' && pair[j]<='9') || (pair[j]>='a' && pair[j]<='f')))valid=false;
                    valid=valid && pair.ToLong(&value,16) && value>=32 && value<=126;
                    if(valid)request.text+=(wxChar)value;
                }
            }
        } else valid=valid && request.method==wxT("document.list_objects");
        valid=valid && Validate(request).empty();
        if (valid) requests.push_back(request);
    }
    valid = valid && !requests.empty();
    if(doc)xmlFreeDoc(doc);
    if(!valid)requests.clear();
    return valid;
}

void RunStartupRequest() {
    if (!wxIsMainThread()) return;
    const char* input = std::getenv("XARA_AUTOMATION_POC_REQUEST");
    const char* output = std::getenv("XARA_AUTOMATION_POC_RESPONSE");
    if (!input || !output) return;
    int fd = open(input, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat info;
    if (fd < 0) return;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
        (info.st_mode & 077) || info.st_size <= 0 || info.st_size > 65536) { close(fd); return; }
    std::string bytes((size_t)info.st_size, '\0'); size_t done = 0;
    while (done < bytes.size()) {
        ssize_t count = read(fd, &bytes[done], bytes.size() - done);
        if (count <= 0) { close(fd); return; }
        done += (size_t)count;
    }
    close(fd);
    // Reserve a new private output before any mutation; never overwrite a file.
    fd = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    xmlDoc* reply = xmlNewDoc(BAD_CAST "1.0");
    xmlNode* response = xmlNewNode(NULL, BAD_CAST "automation_response"); xmlDocSetRootElement(reply, response);
    std::vector<Request> requests;bool transaction=false;
    bool valid=ParseSceneXML(bytes,requests,transaction);
    if (!valid) Property(response, "error", wxT("invalid_request"));
    std::vector<Result> results;
    if(valid)results=ExecuteBatch(requests,transaction);
    if(valid)for (size_t i = 0; i < results.size(); ++i) {
        const Result& result = results[i];
        xmlNode* record = xmlNewChild(response, NULL, BAD_CAST "result", NULL);
        Number(record, "index", (long)i); Property(record, "method", requests[i].method);
        Property(record, "ok", result.ok ? wxT("true") : wxT("false"));
        if (!result.ok) { Property(record, "error", result.error); break; }
        if (result.createdTag) Number(record, "created_tag", (long)result.createdTag);
        for (size_t n = 0; n < result.objects.size(); ++n) {
            const Object& item = result.objects[n]; xmlNode* object = xmlNewChild(record, NULL, BAD_CAST "object", NULL);
            Number(object, "tag", (long)item.tag); Property(object, "type", item.type);
            Number(object, "x0", item.x0); Number(object, "y0", item.y0);
            Number(object, "x1", item.x1); Number(object, "y1", item.y1);
            Property(object, "selected", item.selected ? wxT("true") : wxT("false"));
            Property(object,"fill",item.fill);Property(object,"stroke",item.stroke);
            Number(object,"stroke_width",item.strokeWidth);Number(object,"font_size",item.fontSize);
            Number(object,"anchor_x",item.anchorX);Number(object,"anchor_y",item.anchorY);
            Number(object,"end_x",item.endX);Number(object,"end_y",item.endY);
            Property(object,"text",item.text);
        }
    }
    xmlChar* encoded = NULL; int length = 0; xmlDocDumpMemoryEnc(reply, &encoded, &length, "UTF-8");
    done = 0;
    while (encoded && done < (size_t)length) {
        ssize_t count = write(fd, encoded + done, (size_t)length - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    // Demo-only view adjustment, explicitly requested by the launch helper.
    const char* fit = std::getenv("XARA_AUTOMATION_POC_FIT_DRAWING");
    if (valid && fit && std::string(fit) == "1")
        OpZoomDescriptor::FakeInvoke(OPTOKEN_ZOOMDRAWING);
    close(fd); if (encoded) xmlFree(encoded); xmlFreeDoc(reply);
}
}
