// Explicit local report drafts. No upload or save access occurs on opening.
#include "imgui.h"
#include <SDL3/SDL.h>
#include "../dev_events.h"
#include "../hle/laonline_diag.h"
#include "../game_addresses.h"
#include "../laonline_report_send.h"
#include <string>
#include <stdio.h>
#include <time.h>
#include <random>
#include <stdlib.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include "../laonline_build.h"
#include "option_rows.h"
#include "settings_look.h"
extern "C" void laonline_report_player(char *,int);
extern "C" void laonline_logs_draw(float width,float height);  // V15.1 laonline_logs.mm
static std::string new_report_code(){
 const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
 std::random_device rng;std::uniform_int_distribution<unsigned> pick(0,sizeof(alphabet)-2);
 std::string code="LAO-";for(int i=0;i<5;i++)code+=alphabet[pick(rng)];return code;
}
extern "C" int laonline_connection_status(void);
static std::string quoted(const char *s){
 std::string out="\"";for(const unsigned char *p=(const unsigned char *)s;*p;p++){
  if(*p=='"'||*p=='\\'){out+='\\';out+=(char)*p;}
  else if(*p<32){char b[8];snprintf(b,sizeof b,"\\u%04x",*p);out+=b;}
  else out+=(char)*p;
 }return out+'"';
}
// V12.5: undelivered reports wait in "reports-pending" next to the game and can be sent again.
static std::string pending_body,pending_code;static bool watch_send;static int retry_index=-1;
static std::vector<std::filesystem::path> pending_files(){
 std::vector<std::filesystem::path> out;std::error_code ec;
 for(auto &e:std::filesystem::directory_iterator("reports-pending",ec))if(e.path().extension()==".json")out.push_back(e.path());
 return out;
}
static void save_pending(const std::string &code,const std::string &body){
 if(code.empty()||body.empty())return;std::error_code ec;std::filesystem::create_directories("reports-pending",ec);
 std::ofstream f("reports-pending/laonline-report-"+code+".json",std::ios::binary);f<<body;
}
static void remove_pending(const std::string &code){std::error_code ec;if(!code.empty())std::filesystem::remove("reports-pending/laonline-report-"+code+".json",ec);}
static void retry_next(){
 auto files=pending_files();
 if(files.empty()){retry_index=-1;return;}
 std::ifstream f(files[0],std::ios::binary);std::string body((std::istreambuf_iterator<char>(f)),{});
 std::string name=files[0].stem().string();std::string code=name.rfind("laonline-report-",0)==0?name.substr(16):name;
 laonline_report_clear_status();
 if(body.empty()||!laonline_report_send(body.c_str())){retry_index=-1;return;}
 pending_body=body;pending_code=code;watch_send=true;retry_index=1;
}
/* V13.5: attach a screenshot or video (file picker; checked and sent by laonline_report_send.cpp) */
#include <mutex>
static std::mutex attach_pick_mx;static std::string attach_picked;static bool attach_dialog;
static const SDL_DialogFileFilter attach_filter[]={{"Screenshot or video (PNG, JPG, MP4, MOV, WEBM)","png;jpg;jpeg;mp4;mov;webm"}};
static void SDLCALL attach_chosen(void *,const char *const *files,int){std::lock_guard<std::mutex> lock(attach_pick_mx);if(files&&files[0])attach_picked=files[0];attach_dialog=false;}
static bool reset_form;
extern "C" void report_ui_reset(void){reset_form=true;}
extern "C" void laonline_report_draw(void){
 static const char *cats[]={"Gameplay","Multiplayer / Co-op","Remote Link","Story Synchronization","Enemy Synchronization","Dungeon Synchronization","Items / Pickups","Graphics","Audio","Performance","Controls","Crashes","Other"};
 static const char *sevs[]={"Low","Normal","High","Critical"};
 static int category=1,severity=1;static bool diagnostics=true,network=true;
 static char summary[256]={},expected[1024]={},actual[1024]={},steps[2048]={};
 static std::string preview,report_code;static const char *status="";
 if(reset_form&&laonline_report_status()!=1){reset_form=false;category=severity=1;diagnostics=network=true;summary[0]=expected[0]=actual[0]=steps[0]=0;preview.clear();report_code.clear();status="";laonline_report_clear_status();}
 bool edited=false;int current_status=laonline_report_status();
 // V12.5 layout (reference picture): the form on the left (category, severity, what to include, the text boxes),
 // the buttons and notes on the right. The page header above already says "Report A Bug".
 float sc=ImGui::GetFontSize()/34.f,fs=ImGui::GetFontSize();
 {  // V12.5: LAS Recomp closed after a fatal error last time (runtime/crash_explain.c wrote laonline-last-crash.txt)
  static bool checked=false,have=false;static std::string head,line,log;
  if(!checked){checked=true;std::ifstream f("laonline-last-crash.txt");if(f&&std::getline(f,head)){std::getline(f,line);std::getline(f,log);have=!head.empty();}}
  if(have){
   ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.35f,.12f,.05f,.95f));ImGui::BeginChild("##last-crash",ImVec2(-1,fs*3.4f),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);
   ImGui::TextColored(ImVec4(1,.85f,.5f,1),"Last time LAS Recomp closed: %s",head.c_str());ImGui::SetWindowFontScale(.8f);ImGui::TextWrapped("%s",line.c_str());ImGui::SetWindowFontScale(1);
   if(ImGui::SmallButton("Use it for this report")){category=11;severity=3;diagnostics=true;network=true;snprintf(summary,sizeof summary,"Crash: %.200s",head.c_str());
    snprintf(actual,sizeof actual,"%s\n%s\nLog file: %s\n(Please attach this log file in Discord.)",head.c_str(),line.c_str(),log.c_str());preview.clear();report_code.clear();}
   ImGui::SameLine();if(ImGui::SmallButton("Dismiss")){std::remove("laonline-last-crash.txt");have=false;}
   ImGui::EndChild();ImGui::PopStyleColor();}
 }
 ImGui::BeginDisabled(current_status==1);
 if(ImGui::BeginTable("##report_columns",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerV)){
  ImGui::TableSetupColumn("form",ImGuiTableColumnFlags_WidthStretch,.64f);ImGui::TableSetupColumn("actions",ImGuiTableColumnFlags_WidthStretch,.36f);
  ImGui::TableNextColumn();
  opt_begin("##report",sc);
  edited|=opt_choice("Bug category",&category,cats,IM_ARRAYSIZE(cats),1,"What the problem is about.");
  edited|=opt_choice("Severity",&severity,sevs,IM_ARRAYSIZE(sevs),1,"How badly it affects playing.");
  edited|=opt_toggle("Include recent structured diagnostics",&diagnostics,true,"Includes recent game logs and diagnostic data.");
  edited|=opt_toggle("Include connection state",&network,true,"Includes your current network connection state.");
  opt_end();
  // text boxes: dark rounded frames like the rows above
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.03f,.11f,.22f,1));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.3f,.5f,.75f,.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,6*sc);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10*sc,8*sc));
  auto label=[&](const char *t){ImGui::Dummy(ImVec2(1,4*sc));ImGui::TextUnformatted(t);};
  label("Issue summary");ImGui::SetNextItemWidth(-1);
  edited|=ImGui::InputTextWithHint("##Issue summary","One line: what went wrong?",summary,sizeof summary);
  label("Expected result");edited|=ImGui::InputTextMultiline("##Expected result",expected,sizeof expected,ImVec2(-1,fs*2.6f));
  label("What happened");edited|=ImGui::InputTextMultiline("##What happened",actual,sizeof actual,ImVec2(-1,fs*2.6f));
  label("Reproduction steps");edited|=ImGui::InputTextMultiline("##Reproduction steps",steps,sizeof steps,ImVec2(-1,fs*3.4f));
  ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);
  if(edited){preview.clear();report_code.clear();laonline_report_clear_status();status="";}
  auto prepare_report=[&](){
   laonline_report_clear_status();
   report_code=new_report_code();
   char player[17];laonline_report_player(player,sizeof player);
   char stamp[48];time_t now=time(NULL);struct tm *t=gmtime(&now);strftime(stamp,sizeof stamp,"%Y-%m-%dT%H:%M:%SZ",t);
   const GameVersion *v=game_version();
#ifdef _WIN32
   const char *platform="Windows PC";
#elif defined(__APPLE__)
   const char *platform="macOS PC";
#else
   const char *platform="Linux PC";
#endif
   preview="{\n\"project\":\"LA-Online Recomp\",\"build\":\"" LAO_RELEASE_LABEL "\",\"baseline\":\"" LAO_LAS_BASELINE "\",\n\"timestamp\":"+quoted(stamp)+",\n\"game_revision\":"+quoted(v?v->label:"not loaded")+",\n\"category\":"+quoted(cats[category])+",\"severity\":"+quoted(sevs[severity])+",\n\"summary\":"+quoted(summary)+",\n\"expected\":"+quoted(expected)+",\n\"actual\":"+quoted(actual)+",\n\"steps\":"+quoted(steps);
   preview+=",\n\"platform\":"+quoted(platform);
   preview+=",\n\"report_code\":"+quoted(report_code.c_str())+",\n\"player_name\":"+quoted(player);
   char logcode[10];laonline_diag_log_code(logcode,sizeof logcode);if(logcode[0])preview+=",\n\"log_code\":"+quoted(logcode);
   if(network)preview+=",\n\"connection_state\":"+std::to_string(laonline_connection_status());
   if(diagnostics){char logs[32768];laonline_diag_snapshot(logs,sizeof logs);preview+=",\n\"recent_events_jsonl\":"+quoted(logs);}
   preview+="\n}\n";status="Draft prepared.";
  };
  ImGui::TableNextColumn();
  char endpoint[512];bool configured=laonline_report_endpoint(endpoint,sizeof endpoint)!=0;
  float bw=ImGui::GetContentRegionAvail().x,bh=fs*1.6f;
  {laonline_logs_draw(bw,bh);ImGui::Dummy(ImVec2(1,6*sc));}  // V15.1: all logs in one zip
  if(settings_look::wide_button("Preview report##preview",ImVec2(bw,bh),0,sc))prepare_report();
  ImGui::Dummy(ImVec2(1,2*sc));
  if(settings_look::wide_button("SUBMIT BUG REPORT##submit",ImVec2(bw,bh),1,sc,configured&&summary[0]&&current_status!=1&&current_status!=2)){
   if(preview.empty())prepare_report();
   if(laonline_report_send(preview.c_str())){pending_body=preview;pending_code=report_code;watch_send=true;}
   else{save_pending(report_code,preview);status="Your report couldn't be sent right now, so it was saved. Use \"Send saved reports\" to try again.";}
  }
  if(!summary[0]&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Write an issue summary first.");
  ImGui::Dummy(ImVec2(1,2*sc));
  /* V12.5: a report that can't be delivered is kept (reports-pending) and can be sent later; nothing is lost. */
  {int s2=laonline_report_status();
   if(watch_send&&s2==2){watch_send=false;remove_pending(pending_code);if(retry_index>=0)retry_next();}
   else if(watch_send&&s2==-1){watch_send=false;if(retry_index<0){save_pending(pending_code,pending_body);status="Your report couldn't be sent, so it was saved. Use \"Send saved reports\" to try again.";}else{retry_index=-1;status="Saved reports couldn't be sent yet. Please try again later.";}}
   size_t waiting=pending_files().size();
   if(waiting){char label[64];snprintf(label,sizeof label,"Send saved reports (%zu)",waiting);
    if(settings_look::wide_button(label,ImVec2(bw,bh),0,sc,s2!=1&&configured)){retry_index=0;retry_next();}ImGui::Dummy(ImVec2(1,2*sc));}}
  {static char attach_why[200];
   {std::string picked;{std::lock_guard<std::mutex> lock(attach_pick_mx);picked.swap(attach_picked);}
    if(!picked.empty())laonline_report_attach(picked.c_str(),attach_why,sizeof attach_why);}
   char name[160];long long bytes=0;bool has=laonline_report_attachment(name,sizeof name,&bytes)!=0;
   if(!has){
    if(settings_look::wide_button("Attach screenshot or video##attach",ImVec2(bw,bh),0,sc,!attach_dialog)){
     attach_why[0]=0;attach_dialog=true;SDL_ShowOpenFileDialog(attach_chosen,NULL,NULL,attach_filter,1,NULL,false);}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("PNG / JPG picture or MP4 / MOV / WEBM video, up to 9 MB. Sent with this report.");
   }else{
    char label[220];snprintf(label,sizeof label,"Attached: %.60s (%.1f MB)",name,bytes/1048576.0);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.45f,.95f,.6f,1));ImGui::TextWrapped("%s",label);ImGui::PopStyleColor();
    if(ImGui::SmallButton("Remove attachment"))laonline_report_attach("",attach_why,sizeof attach_why);
   }
   if(attach_why[0])ImGui::TextColored(ImVec4(1,.8f,.4f,1),"%s",attach_why);
   ImGui::Dummy(ImVec2(1,2*sc));}
  if(settings_look::wide_button("SAVE REPORT LOCALLY##save",ImVec2(bw,bh),2,sc)){
   if(preview.empty())prepare_report();
   std::string filename="laonline-report-"+report_code+".json";
   FILE *f=fopen(filename.c_str(),"wb");
   bool ok=f&&fwrite(preview.data(),1,preview.size(),f)==preview.size();if(f&&fclose(f)!=0)ok=false;
   status=ok?"Saved the coded report JSON in the game folder.":"Could not save the draft.";
  }
  if(!preview.empty()){
   ImGui::BeginChild("##report_preview",ImVec2(0,180),ImGuiChildFlags_Borders);ImGui::TextWrapped("%s",preview.c_str());ImGui::EndChild();
  }
  int send_status=laonline_report_status();
  ImGui::SetWindowFontScale(.85f);
  ImGui::TextWrapped("%s",send_status==1?"Sending your report...":send_status==2?"Report sent. Thank you for your feedback!":send_status==-1?"Your report couldn't be sent. It was saved - please try again later.":"Write a short summary, then press Submit Bug Report.");
  if(send_status==-1){char why[512];laonline_report_message(why,sizeof why);if(why[0])ImGui::TextColored(ImVec4(1,.8f,.4f,1),"Why: %s",why);}
  if(!report_code.empty())ImGui::TextWrapped("%s: %s. Paste this code in Discord.",send_status==2?"Your report code is":"Draft report code",report_code.c_str());
  ImGui::TextWrapped("%s",status);
  ImGui::SetWindowFontScale(1);
  ImGui::Separator();
  ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.8f,.87f,.95f,1));ImGui::SetWindowFontScale(.74f);
  ImGui::TextWrapped("Reports go through your co-op server to the developers' Discord. If that fails they try %s.",endpoint);
  ImGui::TextWrapped("Submit sends the current form. Preview and saving locally are optional.");
  ImGui::TextWrapped("The report leaves out passwords, server addresses, chat and save files. Diagnostics cover recorded game events only.");
  ImGui::TextWrapped("A picture or video you attach is sent once, with this report only (needs an updated server).");
  ImGui::TextDisabled("Save attachment: not implemented (nothing read)");
  ImGui::TextWrapped("Event log: %s (%s, %llu events)",dev_events_path(),dev_events_status(),(unsigned long long)dev_events_written());
  ImGui::SetWindowFontScale(1);ImGui::PopStyleColor();
  if(ImGui::SmallButton("Copy event log path"))SDL_SetClipboardText(dev_events_path());
  ImGui::EndTable();
 }
 ImGui::EndDisabled();
}
