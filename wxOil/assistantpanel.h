// SPDX-License-Identifier: GPL-2.0-only
#ifndef XARA_ASSISTANT_PANEL_H
#define XARA_ASSISTANT_PANEL_H
#include "assistantpreset.h"
#include "assistantprovider.h"
#include <wx/filename.h>
#include <libxml/parser.h>
#include "document.h"
#include "ophist.h"
#include "zoomops.h"
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <cstdio>
#include <cstdlib>
#include <string>

class XaraAssistantPanel : public wxPanel {
    wxTextCtrl* m_prompt;
    wxTextCtrl* m_status;
    wxChoice* m_provider;
    wxTimer m_testTimer;
    unsigned m_testTicks;
    std::vector<XaraAutomation::Result> m_results;
    wxTimer m_providerTimer;
    wxProcess* m_process;
    long m_pid;
    std::string m_reply;
    bool m_cancelled;
    unsigned m_networkTicks;
    wxString m_endpoint,m_model,m_key;
    AssistantRevision m_revision;
    unsigned long m_requestRevision;
    Document* m_requestDoc;
    Spread* m_requestSpread;
    Layer* m_requestLayer;
    std::vector<XaraAutomation::Request> m_candidate;
    wxButton* m_generate;
    wxButton* m_apply;
    unsigned m_providerSelfStage,m_heartbeat;
public:
    XaraAssistantPanel(wxWindow* parent):wxPanel(parent),m_testTimer(this),m_testTicks(0),m_providerTimer(this,wxID_HIGHEST+271),m_process(NULL),m_pid(0),m_cancelled(false),m_networkTicks(0),m_requestRevision(0),m_requestDoc(NULL),m_requestSpread(NULL),m_requestLayer(NULL),m_providerSelfStage(0),m_heartbeat(0) {
        wxBoxSizer* s=new wxBoxSizer(wxVERTICAL);
        wxStaticText* heading=new wxStaticText(this,wxID_ANY,wxT("AI drawing requests and offline demos"));
        heading->Wrap(280);s->Add(heading,0,wxALL,8);
        m_provider=new wxChoice(this,wxID_ANY);
        m_provider->Append(wxT("Predefined demo"));m_provider->Append(wxT("OpenAI-compatible"));
        m_provider->Append(wxT("Anthropic (not connected)"));m_provider->Append(wxT("Ollama / local"));
        m_provider->SetSelection(0);s->Add(m_provider,0,wxEXPAND|wxLEFT|wxRIGHT,8);
        wxStaticText* help=new wxStaticText(this,wxID_ANY,wxT("Describe rectangles/circles, or use demo.scene, demo.circle or demo.rectangle"));
        help->Wrap(280);s->Add(help,0,wxALL,8);
        m_prompt=new wxTextCtrl(this,wxID_ANY,wxT("demo.scene"),wxDefaultPosition,wxSize(300,95),wxTE_MULTILINE);
        s->Add(m_prompt,1,wxEXPAND|wxLEFT|wxRIGHT,8);
        wxBoxSizer* buttons=new wxBoxSizer(wxHORIZONTAL);
        wxButton* generate=new wxButton(this,wxID_ANY,wxT("Generate"));
        wxButton* settings=new wxButton(this,wxID_ANY,wxT("Settings..."));
        m_generate=generate;
        m_apply=new wxButton(this,wxID_ANY,wxT("Apply"));m_apply->Enable(false);
        wxButton* cancel=new wxButton(this,wxID_ANY,wxT("Cancel"));
        m_apply->Bind(wxEVT_BUTTON,&XaraAssistantPanel::ApplyCandidate,this);
        cancel->Bind(wxEVT_BUTTON,&XaraAssistantPanel::CancelProvider,this);
        buttons->Add(m_apply,0,wxRIGHT,4);buttons->Add(cancel,0,wxRIGHT,4);
        buttons->Add(generate,0,wxRIGHT,8);buttons->Add(settings);s->Add(buttons,0,wxALL,8);
        m_status=new wxTextCtrl(this,wxID_ANY,wxT("Adds editable objects to the active document. One Undo removes the preset."),
            wxDefaultPosition,wxSize(300,115),wxTE_MULTILINE|wxTE_READONLY);
        s->Add(m_status,1,wxEXPAND|wxALL,8);SetSizer(s);
        generate->Bind(wxEVT_BUTTON,&XaraAssistantPanel::Generate,this);
        settings->Bind(wxEVT_BUTTON,&XaraAssistantPanel::Settings,this);
        Bind(wxEVT_TIMER,&XaraAssistantPanel::SelfTest,this,m_testTimer.GetId());
        Bind(wxEVT_TIMER,&XaraAssistantPanel::PollProvider,this,m_providerTimer.GetId());
        Bind(wxEVT_END_PROCESS,&XaraAssistantPanel::ProviderEnded,this);
        m_endpoint=wxT("http://127.0.0.1:11434");
        const char* model=std::getenv("XARA_OLLAMA_MODEL");if(model)m_model=wxString::FromUTF8(model);
        // Explicit automated test only: creates objects in a disposable blank document.
        const char* test=std::getenv("XARA_ASSISTANT_SELF_TEST");
        if((test && std::string(test)=="1") || std::getenv("XARA_ASSISTANT_PROVIDER_SELF_TEST"))m_testTimer.Start(100);
    }
    ~XaraAssistantPanel() {
        m_providerTimer.Stop();
        if(m_process){m_process->Detach();wxKill(m_pid,wxSIGTERM,NULL,wxKILL_CHILDREN);m_process=NULL;}
    }
private:
    void Generate(wxCommandEvent&) {
        if(m_process){m_status->SetValue(wxT("Request already running."));return;}
        m_candidate.clear();m_apply->Enable(false);
        m_results.clear();
        if(m_provider->GetSelection()!=0 && !m_prompt->GetValue().StartsWith(wxT("demo."))) {StartProvider();return;}
        std::vector<XaraAutomation::Request> requests;
        if(!XaraAssistant::Preset(m_prompt->GetValue(),requests)) {
            m_status->SetValue(wxT("Unsupported request. Use exactly demo.scene, demo.circle or demo.rectangle. No changes made."));return;
        }
        m_results=XaraAutomation::ExecuteBatch(requests,true);
        for(size_t i=0;i<m_results.size();++i)if(!m_results[i].ok) {
            m_status->SetValue(wxT("Request failed: ")+m_results[i].error);return;
        }
        if(m_results.size()!=requests.size()){m_status->SetValue(wxT("Request incomplete."));return;}
        wxString status=wxString::Format(wxT("Created %u editable objects in the active document. One Undo / Redo applies to this preset.\n"),
            (unsigned)(requests.size()-2));
        const std::vector<XaraAutomation::Object>& objects=m_results.back().objects;
        for(size_t n=0;n<objects.size();++n)status+=objects[n].type+wxT("\n");
        m_status->SetValue(status);
        OpZoomDescriptor::FakeInvoke(OPTOKEN_ZOOMDRAWING);
    }
    void Settings(wxCommandEvent&) {
        if(m_process){m_status->SetValue(wxT("Cancel the pending request before configuring."));return;}
        wxDialog d(this,wxID_ANY,wxT("AI Assistant settings"));wxBoxSizer* box=new wxBoxSizer(wxVERTICAL);
        box->Add(new wxStaticText(&d,wxID_ANY,wxT("Session-only settings. Cloud providers receive your prompt.\nOllama endpoint: http://127.0.0.1:11434\nCompatible endpoint: HTTPS base URL ending /v1")),0,wxALL,10);
        box->Add(new wxStaticText(&d,wxID_ANY,wxT("Endpoint")),0,wxLEFT,10);
        wxTextCtrl* endpoint=new wxTextCtrl(&d,wxID_ANY,m_endpoint);box->Add(endpoint,0,wxEXPAND|wxALL,10);
        box->Add(new wxStaticText(&d,wxID_ANY,wxT("Model (must be installed / available)")),0,wxLEFT,10);
        wxTextCtrl* model=new wxTextCtrl(&d,wxID_ANY,m_model);box->Add(model,0,wxEXPAND|wxALL,10);
        box->Add(new wxStaticText(&d,wxID_ANY,wxT("API key (optional for local inference)")),0,wxLEFT,10);
        wxTextCtrl* key=new wxTextCtrl(&d,wxID_ANY,m_key,wxDefaultPosition,wxDefaultSize,wxTE_PASSWORD);box->Add(key,0,wxEXPAND|wxALL,10);
        box->Add(d.CreateButtonSizer(wxOK|wxCANCEL),0,wxALIGN_RIGHT|wxALL,10);d.SetSizerAndFit(box);
        if(d.ShowModal()==wxID_OK){m_endpoint=endpoint->GetValue();m_model=model->GetValue();m_key=key->GetValue();m_candidate.clear();m_apply->Enable(false);}
    }
    bool SameContext() {
        return m_requestRevision==m_revision.revision && m_requestDoc && Document::GetSelected()==m_requestDoc &&
            Document::GetCurrent()==m_requestDoc && Document::GetSelectedSpread()==m_requestSpread &&
            m_requestSpread && m_requestSpread->FindActiveLayer()==m_requestLayer;
    }
    void StartProvider() {
        if(m_provider->GetSelection()==2){m_status->SetValue(wxT("Anthropic adapter is planned, not implemented."));return;}
        if(!Document::GetSelected() || !Document::GetSelectedSpread() || m_model.empty()) {m_status->SetValue(wxT("Open a document and set a model in Settings."));return;}
        wxString provider=m_provider->GetSelection()==3?wxT("ollama"):wxT("openai-compatible");
        std::string input=AssistantInput(provider,m_endpoint,m_model,m_key,m_prompt->GetValue());
        if(input.size()>4096){m_status->SetValue(wxT("Prompt/settings too large."));return;}
        const char* root=std::getenv("XARA_ASSISTANT_ROOT");const char* python=std::getenv("XARA_ASSISTANT_PYTHON");
        if(!root || !python){m_status->SetValue(wxT("Launch with XARA_ASSISTANT_ROOT and XARA_ASSISTANT_PYTHON configured."));return;}
        wxString worker=wxString::FromUTF8(root)+wxT("/automation/assistant/worker.py");
        if(!wxFileExists(worker)){m_status->SetValue(wxT("Provider worker is missing."));return;}
        m_requestDoc=Document::GetSelected();m_requestSpread=Document::GetSelectedSpread();m_requestLayer=m_requestSpread->FindActiveLayer();m_requestRevision=m_revision.revision;
        m_process=new wxProcess(this);m_process->Redirect();
        wxCharBuffer exe=wxString::FromUTF8(python).utf8_str(),script=worker.utf8_str();
        char* argv[]={exe.data(),script.data(),NULL};m_pid=wxExecute(argv,wxEXEC_ASYNC,m_process);
        if(m_pid<=0){delete m_process;m_process=NULL;m_status->SetValue(wxT("Cannot start provider worker."));return;}
        m_process->GetOutputStream()->Write(input.data(),input.size());m_process->CloseOutput();
        m_reply.clear();m_cancelled=false;m_networkTicks=0;m_generate->Enable(false);m_apply->Enable(false);
        m_status->SetValue(wxT("Generating asynchronously. Continue viewing; edits invalidate this request."));m_providerTimer.Start(50);
    }
    void DrainProvider() {
        if(!m_process)return;
        char bytes[4096];
        while(m_process->IsInputAvailable()) {
            m_process->GetInputStream()->Read(bytes,sizeof(bytes));size_t n=m_process->GetInputStream()->LastRead();if(!n)break;
            if(m_reply.size()+n>65536){m_cancelled=true;wxKill(m_pid,wxSIGTERM,NULL,wxKILL_CHILDREN);break;}
            m_reply.append(bytes,n);
        }
        // Discard worker stderr without exposing provider bodies or credentials.
        while(m_process->IsErrorAvailable()){m_process->GetErrorStream()->Read(bytes,sizeof(bytes));if(!m_process->GetErrorStream()->LastRead())break;}
    }
    void PollProvider(wxTimerEvent&) {
        DrainProvider();
        if(++m_networkTicks>2400 || !SameContext()) {
            m_cancelled=true;wxKill(m_pid,wxSIGTERM,NULL,wxKILL_CHILDREN);
            m_status->SetValue(wxT("Request cancelled: timeout or document context changed."));
        }
    }
    void CancelProvider(wxCommandEvent&) {
        m_candidate.clear();m_apply->Enable(false);
        if(m_process){m_cancelled=true;wxKill(m_pid,wxSIGTERM,NULL,wxKILL_CHILDREN);}
        m_status->SetValue(wxT("Cancelled. No generated objects applied."));
    }
    void ProviderEnded(wxProcessEvent& event) {
        if(!m_process || event.GetPid()!=m_pid)return;
        DrainProvider();m_providerTimer.Stop();delete m_process;m_process=NULL;m_generate->Enable(true);
        bool transaction=false;
        if(m_cancelled || !SameContext()){m_status->SetValue(wxT("Reply discarded: cancelled or document changed."));return;}
        if(event.GetExitCode()!=0 || !XaraAutomation::ParseSceneXML(m_reply,m_candidate,transaction) || !transaction) {
            m_candidate.clear();
            wxString error=wxT("provider_or_validation_failure");
            if(m_reply.size()<256 && m_reply.find("<!")==std::string::npos && m_reply.find('&')==std::string::npos) {
                xmlDoc* reply=xmlReadMemory(m_reply.data(),m_reply.size(),"reply.xml",NULL,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
                xmlNode* root=reply?xmlDocGetRootElement(reply):NULL;
                if(root && xmlStrEqual(root->name,BAD_CAST "assistant_error")) {
                    xmlChar* code=xmlGetProp(root,BAD_CAST "code");if(code){error=wxString::FromUTF8((char*)code);xmlFree(code);}
                }
                if(reply)xmlFreeDoc(reply);
            }
            m_status->SetValue(wxT("No changes made: ")+error+wxT(". Check Settings / endpoint / model."));return;
        }
        for(size_t n=0;n<m_candidate.size();++n)if(m_candidate[n].method!=wxT("object.create_rectangle") && m_candidate[n].method!=wxT("object.create_ellipse")) {
            m_candidate.clear();m_status->SetValue(wxT("Unsupported provider object type."));return;
        }
        m_apply->Enable(true);
        wxString summary=wxString::Format(wxT("Validated %u objects. Click Apply to insert (one Undo).\n"),(unsigned)m_candidate.size());
        for(size_t i=0;i<m_candidate.size();++i)summary+=m_candidate[i].method+wxT(" ")+m_candidate[i].fill+wxT("\n");
        m_status->SetValue(summary);
    }
    void ApplyCandidate(wxCommandEvent&) {
        if(m_candidate.empty())return;
        if(!SameContext()){m_candidate.clear();m_apply->Enable(false);m_status->SetValue(wxT("Document changed; generate again."));return;}
        std::vector<XaraAutomation::Request> candidate;m_candidate.swap(candidate);m_apply->Enable(false);
        m_results=XaraAutomation::ExecuteBatch(candidate,true);
        bool ok=m_results.size()==candidate.size();for(size_t i=0;i<m_results.size();++i)ok=ok && m_results[i].ok;
        if(ok){m_status->SetValue(wxT("Created native editable objects. One Undo/Redo applies to the scene."));OpZoomDescriptor::FakeInvoke(OPTOKEN_ZOOMDRAWING);}
        else m_status->SetValue(wxT("Native transaction failed. No successful scene claimed."));
    }
    void ProviderSelfTest() {
        XaraAutomation::Request list;list.method=wxT("document.list_objects");wxCommandEvent e;
        if(m_providerSelfStage==0) {
            if(!Document::GetSelected())return;
            XaraAutomation::Result empty=XaraAutomation::Execute(list);
            if(!empty.ok || !empty.objects.empty() || Document::GetSelected()->IsModified()) {m_testTimer.Stop();std::fprintf(stderr,"ASSISTANT_PROVIDER_TEST_FAIL needs blank document\n");return;}
            m_prompt->SetValue(wxT("demo.rectangle"));m_provider->SetSelection(0);Generate(e);
            m_prompt->SetValue(wxT("Draw a red circle and a blue rectangle next to it."));m_provider->SetSelection(3);
            const char* endpoint=std::getenv("XARA_OLLAMA_TEST_ENDPOINT");if(endpoint)m_endpoint=wxString::FromUTF8(endpoint);
            StartProvider();m_providerSelfStage=1;return;
        }
        if(m_process){
            ++m_heartbeat;
            if(std::getenv("XARA_ASSISTANT_PROVIDER_TEST_STALE") && m_heartbeat==3) {
                std::vector<XaraAutomation::Request> r;XaraAssistant::Preset(wxT("demo.circle"),r);XaraAutomation::ExecuteBatch(r,true);
            }
            return;
        }
        if(std::getenv("XARA_ASSISTANT_PROVIDER_TEST_STALE")) {
            m_testTimer.Stop();XaraAutomation::Result result=XaraAutomation::Execute(list);
            bool ok=m_cancelled && m_candidate.empty() && result.ok && result.objects.size()==2;
            std::fprintf(stderr,"ASSISTANT_PROVIDER_TEST_%s stale-reply no-generated-mutation async-heartbeat=%u\n",ok?"PASS":"FAIL",m_heartbeat);
            Document* d=Document::GetSelected();if(d)d->SetModified(FALSE);CallAfter(&XaraAssistantPanel::CloseTestFrame);return;
        }
        m_testTimer.Stop();bool ok=m_candidate.size()==2 && SameContext() && m_heartbeat>5;
        if(ok)ApplyCandidate(e);
        XaraAutomation::Result after=XaraAutomation::Execute(list);
        ok=ok && after.ok && after.objects.size()==3;
        if(after.objects.size()==3)ok=ok && after.objects[1].type==wxT("NodeEllipse") && after.objects[1].fill==wxT("#FF0000") &&
            after.objects[2].type==wxT("NodeRect") && after.objects[2].fill==wxT("#0000FF");
        Document* doc=Document::GetSelected();
        if(ok){ok=doc->GetOpHistory().UndoPrev() && XaraAutomation::Execute(list).objects.size()==1;
            ok=ok && doc->GetOpHistory().RedoNext() && XaraAutomation::Execute(list).objects.size()==3;}
        std::fprintf(stderr,"ASSISTANT_PROVIDER_TEST_%s current-document colours native-objects undo redo async-heartbeat=%u\n",ok?"PASS":"FAIL",m_heartbeat);
        if(doc)doc->SetModified(FALSE);CallAfter(&XaraAssistantPanel::CloseTestFrame);
    }
    void SelfTest(wxTimerEvent&) {
        if(std::getenv("XARA_ASSISTANT_PROVIDER_SELF_TEST")){ProviderSelfTest();return;}
        if(!Document::GetSelected() || Document::GetCurrent()!=Document::GetSelected()) {
            if(++m_testTicks<80)return;
            m_testTimer.Stop();std::fprintf(stderr,"ASSISTANT_SELF_TEST_FAIL no document\n");CallAfter(&XaraAssistantPanel::CloseTestFrame);return;
        }
        m_testTimer.Stop();Document* doc=Document::GetSelected();
        XaraAutomation::Request list;list.method=wxT("document.list_objects");
        XaraAutomation::Result before=XaraAutomation::Execute(list);
        // Never discard pre-existing content or unsaved user edits in test mode.
        // The smoke launcher supplies private preferences and no file arguments.
        if(!before.ok || !before.objects.empty() || doc->IsModified()) {
            std::fprintf(stderr,"ASSISTANT_SELF_TEST_FAIL requires blank unmodified document\n");
            return;
        }
        unsigned undoBefore=doc->GetOpHistory().GetNumUndoSteps();
        wxCommandEvent e;bool ok=before.ok && before.objects.empty();
        // Rejected input and unavailable provider must not change the document.
        m_prompt->SetValue(wxT("draw arbitrary code"));Generate(e);
        ok=ok && m_results.empty() && XaraAutomation::Execute(list).objects.empty();
        m_prompt->SetValue(wxT("draw a circle"));m_provider->SetSelection(2);Generate(e);
        ok=ok && m_results.empty() && XaraAutomation::Execute(list).objects.empty();
        m_prompt->SetValue(wxT("demo.scene"));m_provider->SetSelection(0);Generate(e);
        ok=ok && Document::GetSelected()==doc && m_results.size()==6;
        for(size_t i=0;i<m_results.size();++i)ok=ok && m_results[i].ok;
        if(m_results.size()==6) {
            const std::vector<XaraAutomation::Object>& o=m_results.back().objects;
            ok=ok && o.size()==4;
            if(o.size()==4)ok=ok && o[0].type==wxT("NodeRect") && o[0].fill==wxT("#185ADB") &&
                o[0].x0==72000 && o[0].y0==180000 && o[0].x1==252000 && o[0].y1==288000 &&
                o[1].type==wxT("NodeEllipse") && o[1].fill==wxT("#D4AF37") &&
                o[1].x0==288000 && o[1].y0==180000 && o[1].x1==396000 && o[1].y1==288000 &&
                o[2].type==wxT("NodePath") && o[2].stroke==wxT("#E53935") && o[2].strokeWidth==3000 &&
                o[2].anchorX==72000 && o[2].anchorY==144000 && o[2].endX==396000 && o[2].endY==108000 &&
                o[3].type==wxT("TextStory") && o[3].text==wxT("PROJECT VENGEANCE") &&
                o[3].fill==wxT("#202020") && o[3].fontSize==24000 && o[3].anchorX==72000 && o[3].anchorY==54000;
        }
        ok=ok && doc->GetOpHistory().GetNumUndoSteps()==undoBefore+1;
        bool undo=doc->GetOpHistory().UndoPrev();XaraAutomation::Result undone=XaraAutomation::Execute(list);
        bool redo=doc->GetOpHistory().RedoNext();XaraAutomation::Result redone=XaraAutomation::Execute(list);
        ok=ok && undo && undone.ok && undone.objects.empty() && redo && redone.ok && redone.objects.size()==4;
        // Another submission targets the same existing document and preserves its objects.
        m_prompt->SetValue(wxT("demo.circle"));Generate(e);
        ok=ok && Document::GetSelected()==doc && m_results.size()==3 && m_results.back().ok && m_results.back().objects.size()==5;
        bool secondUndo=doc->GetOpHistory().UndoPrev();
        ok=ok && secondUndo && XaraAutomation::Execute(list).objects.size()==4;
        std::fprintf(stderr,"ASSISTANT_SELF_TEST_%s gui-thread scene/model/rejection/current-document/undo/redo\n",ok?"PASS":"FAIL");
        // Only this test's initially blank document is discarded. ExitMainLoop
        // bypassed normal frame closure and left OnExit with an unsaved document.
        // wxDocManager still calls OnSaveModified during teardown (even forced).
        // Keep the event loop alive for document/dialog/window destruction.
        doc->SetModified(FALSE);
        CallAfter(&XaraAssistantPanel::CloseTestFrame);
    }
    void CloseTestFrame() {
        wxFrame* frame=wxDynamicCast(GetParent(),wxFrame);
        std::fprintf(stderr,"ASSISTANT_SELF_TEST_CLOSE_REQUEST normal frame closure\n");
        if(!frame || !frame->Close())
            std::fprintf(stderr,"ASSISTANT_SELF_TEST_FAIL close vetoed or frame missing\n");
    }
};
#endif
