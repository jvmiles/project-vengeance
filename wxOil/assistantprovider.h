// SPDX-License-Identifier: GPL-2.0-only
#ifndef XARA_ASSISTANT_PROVIDER_H
#define XARA_ASSISTANT_PROVIDER_H
#include "pump.h"
#include "msg.h"
#include "docmsgs.h"
#include "layer.h"
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <libxml/tree.h>
// Conservative revision token: operation and document transitions invalidate replies.
class AssistantRevision : public MessageHandler {
public:
    unsigned long revision;
    AssistantRevision():revision(0) {}
    virtual MsgResult Message(Msg* msg) {
        if(MESSAGE_IS_A(msg,OpMsg) || MESSAGE_IS_A(msg,DocChangingMsg))++revision;
        return OK;
    }
};
inline std::string AssistantInput(const wxString& provider,const wxString& endpoint,
                                 const wxString& model,const wxString& key,const wxString& prompt) {
    xmlDoc* d=xmlNewDoc(BAD_CAST "1.0");xmlNode* r=xmlNewNode(NULL,BAD_CAST "generate");xmlDocSetRootElement(d,r);
    const char* names[]={"provider","endpoint","model","key","prompt"};
    wxString values[]={provider,endpoint,model,key,prompt};
    for(int i=0;i<5;++i){wxCharBuffer v=values[i].utf8_str();xmlNewTextChild(r,NULL,BAD_CAST names[i],BAD_CAST v.data());}
    xmlChar* data=NULL;int size=0;xmlDocDumpMemoryEnc(d,&data,&size,"UTF-8");
    std::string out((char*)data,size);xmlFree(data);xmlFreeDoc(d);return out;
}
#endif
