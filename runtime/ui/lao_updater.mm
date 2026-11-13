// V13 Update Now: download the newer LA-Online player package from the GitHub release, unpack it, and hand it to
// the package's own INSTALL-COOP.ps1 -NonInteractive (the installer players already ran by hand) after the game closes.
// Name, lobby, server, password and saves stay: -NonInteractive reuses every value already in laonline.ini, and
// INSTALL-RUNTIME backs up saves, laonline.ini and the old lasrecomp.exe before it changes anything.
#include "lao_updater.h"
#include "imgui.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace lao_updater {
static std::atomic<int> st(IDLE);
static std::atomic<float> prog(0);
static std::mutex mx;
static std::string msg="", version_to;
static void set_msg(const std::string &m){std::lock_guard<std::mutex> l(mx);msg=m;}
static void fail(const std::string &m){set_msg(m);st=FAILED;}
int state(){return st.load();}
float progress(){return prog.load();}
std::string message(){std::lock_guard<std::mutex> l(mx);return msg;}
#ifdef LAO_UPDATER_PREVIEW
void preview_state(int s,float p,const char *m){st=s;prog=p;set_msg(m);}
#endif
bool busy(){int s=st.load();return s==DOWNLOADING||s==PREPARING||s==INSTALLING;}

#ifdef _WIN32
static std::wstring wide(const std::string &s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,NULL,0);std::wstring w(n>0?n-1:0,L'\0');if(n>1)MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&w[0],n);return w;}
static std::wstring game_dir(){wchar_t p[MAX_PATH*2];DWORD n=GetModuleFileNameW(NULL,p,MAX_PATH*2);if(!n||n>=MAX_PATH*2)return L"";std::wstring s(p,n);size_t k=s.find_last_of(L"\\/");return k==std::wstring::npos?L"":s.substr(0,k);}
static bool remove_tree(const std::wstring &dir){
 WIN32_FIND_DATAW f;HANDLE h=FindFirstFileW((dir+L"\\*").c_str(),&f);
 if(h!=INVALID_HANDLE_VALUE){do{std::wstring n=f.cFileName;if(n==L"."||n==L"..")continue;std::wstring p=dir+L"\\"+n;
   if(f.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){RemoveDirectoryW(p.c_str());DeleteFileW(p.c_str());}
   else if(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)remove_tree(p);else{SetFileAttributesW(p.c_str(),FILE_ATTRIBUTE_NORMAL);DeleteFileW(p.c_str());}
  }while(FindNextFileW(h,&f));FindClose(h);}
 return RemoveDirectoryW(dir.c_str())||GetLastError()==ERROR_FILE_NOT_FOUND;
}
// Finds <root>\...\_support\INSTALL-RUNTIME.ps1 at most two folders deep; returns the package folder.
static std::wstring find_package(const std::wstring &root,int depth){
 if(GetFileAttributesW((root+L"\\_support\\INSTALL-RUNTIME.ps1").c_str())!=INVALID_FILE_ATTRIBUTES)return root;
 if(depth<=0)return L"";
 WIN32_FIND_DATAW f;HANDLE h=FindFirstFileW((root+L"\\*").c_str(),&f);std::wstring hit;
 if(h!=INVALID_HANDLE_VALUE){do{std::wstring n=f.cFileName;if(n==L"."||n==L".."||!(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(f.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
   hit=find_package(root+L"\\"+n,depth-1);}while(hit.empty()&&FindNextFileW(h,&f));FindClose(h);}
 return hit;
}
static bool run_hidden(const std::wstring &cmd,DWORD wait_ms,DWORD *code){
 STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};std::wstring c=cmd;
 if(!CreateProcessW(NULL,&c[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi))return false;
 DWORD w=WaitForSingleObject(pi.hProcess,wait_ms);if(w!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return false;}
 GetExitCodeProcess(pi.hProcess,code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;
}
static std::string read_small(const std::wstring &path){
 HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(h==INVALID_HANDLE_VALUE)return {};
 char buf[4096];DWORD got=0;BOOL ok=ReadFile(h,buf,sizeof buf-1,&got,NULL);CloseHandle(h);return ok?std::string(buf,got):std::string();
}
static bool write_text(const std::wstring &path,const std::string &text){
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE)return false;
 DWORD n=0;BOOL ok=WriteFile(h,text.data(),(DWORD)text.size(),&n,NULL)&&n==text.size();CloseHandle(h);return ok;
}
// Downloads url to path (0..0.88 of the bar). GitHub answers a release download with a redirect to its file host;
// WinHTTP follows HTTPS -> HTTPS redirects by itself.
static bool download(const std::string &url,const std::wstring &path,long long expected,std::string &error){
 std::wstring w=wide(url);URL_COMPONENTS u{};u.dwStructSize=sizeof u;wchar_t host[256],up[2048];u.lpszHostName=host;u.dwHostNameLength=256;u.lpszUrlPath=up;u.dwUrlPathLength=2048;
 if(!WinHttpCrackUrl(w.c_str(),0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS){error="Bad download address.";return false;}
 HINTERNET s=WinHttpOpen(L"LA-Online-Updater",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!s){error="Could not start the download.";return false;}
 WinHttpSetTimeouts(s,15000,15000,20000,30000);
 HINTERNET c=WinHttpConnect(s,host,u.nPort,0),q=c?WinHttpOpenRequest(c,L"GET",up,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):NULL;
 DWORD status=0,len=sizeof status;
 bool ok=q&&WinHttpSendRequest(q,L"Accept: application/octet-stream\r\n",(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(q,NULL)&&
  WinHttpQueryHeaders(q,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX)&&status==200;
 if(!ok){char t[120];snprintf(t,sizeof t,"Download failed (HTTP %lu, network error %lu). Check your internet and try again.",(unsigned long)status,(unsigned long)GetLastError());error=t;}
 long long total=expected;{wchar_t cl[32];DWORD cls=sizeof cl;if(ok&&WinHttpQueryHeaders(q,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,cl,&cls,WINHTTP_NO_HEADER_INDEX)){long long v=_wtoi64(cl);if(v>0)total=v;}}
 if(ok&&expected>0&&total!=expected){ok=false;error="The download size does not match the release. Try again later.";}
 HANDLE f=ok?CreateFileW(path.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
 if(ok&&f==INVALID_HANDLE_VALUE){ok=false;error="Could not write the update into the game folder (is the disk full?).";}
 long long have=0;std::string buf(256*1024,'\0');
 while(ok){DWORD avail=0,got=0,put=0;if(!WinHttpQueryDataAvailable(q,&avail)){ok=false;error="The download was interrupted. Try again.";break;}if(!avail)break;
  if(avail>buf.size())avail=(DWORD)buf.size();if(!WinHttpReadData(q,&buf[0],avail,&got)){ok=false;error="The download was interrupted. Try again.";break;}if(!got)break;
  if(!WriteFile(f,buf.data(),got,&put,NULL)||put!=got){ok=false;error="Could not write the update (is the disk full?).";break;}
  have+=got;if(total>0)prog=0.88f*(float)((double)have/(double)total);if(have>900ll*1024*1024){ok=false;error="Download too large.";}}
 if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
 if(ok&&total>0&&have!=total){ok=false;error="The download was incomplete. Try again.";}
 if(q)WinHttpCloseHandle(q);if(c)WinHttpCloseHandle(c);WinHttpCloseHandle(s);
 if(!ok)DeleteFileW(path.c_str());
 return ok;
}
static const char *k_apply=
 "param([string]$Game,[string]$Package,[int]$GamePid,[string]$Version)\r\n"
 "$ErrorActionPreference='Stop'\r\n"
 "try{$Host.UI.RawUI.WindowTitle='LA-Online update'}catch{}\r\n"
 "$Log=Join-Path $Game 'LA-Online-update\\update-log.txt'\r\n"
 "try{Start-Transcript -LiteralPath $Log -Force|Out-Null}catch{}\r\n"
 "Write-Host ('Updating LA-Online to V'+$Version+'. Your name, lobby, room and saves stay the same.') -ForegroundColor Cyan\r\n"
 "Write-Host 'This window closes and the game opens again by itself.'\r\n"
 "$ok=$false\r\n"
 "try{\r\n"
 " Write-Progress -Activity 'Updating LA-Online' -Status 'Waiting for the game to close... 96%' -PercentComplete 96\r\n"
 " $p=Get-Process -Id $GamePid -ErrorAction SilentlyContinue; if($p){$p|Wait-Process -Timeout 60 -ErrorAction SilentlyContinue}\r\n"
 " for($i=0;$i -lt 40 -and (Get-Process -Name 'lasrecomp' -ErrorAction SilentlyContinue);$i++){Start-Sleep -Milliseconds 500}\r\n"
 " Write-Progress -Activity 'Updating LA-Online' -Status 'Installing... 98%' -PercentComplete 98\r\n"
 " $Coop=Join-Path $Package '_support\\INSTALL-COOP.ps1'\r\n"
 " if(Test-Path -LiteralPath $Coop){& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Coop -GameFolder $Game -NonInteractive}\r\n"
 " else{& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Package '_support\\INSTALL-RUNTIME.ps1') -GameFolder $Game}\r\n"
 " if($LASTEXITCODE -ne 0){throw 'The installer stopped (see the message above). Nothing was changed; your current version still works.'}\r\n"
 " Write-Progress -Activity 'Updating LA-Online' -Status 'Done! 100%' -PercentComplete 100\r\n"
 " Set-Content -LiteralPath (Join-Path $Game 'LA-Online-update\\updated.txt') -Value $Version\r\n"
 " Remove-Item -LiteralPath (Join-Path $Game 'LA-Online-update\\package.zip') -Force -ErrorAction SilentlyContinue\r\n"
 " Remove-Item -LiteralPath (Join-Path $Game 'LA-Online-update\\package') -Recurse -Force -ErrorAction SilentlyContinue\r\n"
 " Write-Host ('Done! LA-Online V'+$Version+' is installed.') -ForegroundColor Green\r\n"
 " $ok=$true\r\n"
 "}catch{Write-Host ('UPDATE STOPPED: '+$_.Exception.Message) -ForegroundColor Red}\r\n"
 "try{Stop-Transcript|Out-Null}catch{}\r\n"
 "Start-Process -FilePath (Join-Path $Game 'lasrecomp.exe') -WorkingDirectory $Game\r\n"
 "if(-not $ok){Write-Host 'The game was reopened on your current version. Log: LA-Online-update\\update-log.txt';Read-Host 'Press Enter to close'}else{Start-Sleep -Seconds 2}\r\n";

static void worker(std::string url,long long size,std::string tag,int rank){
 try{
  std::wstring dir=game_dir();if(dir.empty()){fail("Could not find the game folder.");return;}
  if(GetFileAttributesW((dir+L"\\lasrecomp.exe").c_str())==INVALID_FILE_ATTRIBUTES){fail("lasrecomp.exe was not found next to the game. Use INSTALL-CO-OP.bat this one time.");return;}
  std::wstring work=dir+L"\\LA-Online-update";remove_tree(work);
  if(!CreateDirectoryW(work.c_str(),NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){fail("Could not make the LA-Online-update folder in the game folder.");return;}
  set_msg("Downloading "+tag+"...");std::string error;std::wstring zip=work+L"\\package.zip";
  if(!download(url,zip,size,error)){fail(error);return;}
  if(read_small(zip).compare(0,2,"PK")!=0){fail("The downloaded file is not a zip. Nothing was changed.");return;}
  st=PREPARING;prog=0.90f;set_msg("Unpacking...");
  std::wstring unpack=work+L"\\package";
  DWORD code=1;std::wstring cmd=L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '"+zip+L"' -DestinationPath '"+unpack+L"' -Force\"";
  if(!run_hidden(cmd,180000,&code)||code!=0){fail("Could not unpack the update. Nothing was changed.");return;}
  prog=0.94f;
  std::wstring pkg=find_package(unpack,2);
  if(pkg.empty()){fail("This download has no INSTALL-RUNTIME (wrong file attached to the release). Nothing was changed.");return;}
  std::string rel=read_small(pkg+L"\\_support\\RELEASE.json");size_t v=rel.find("\"version\"");std::string ver;
  if(v!=std::string::npos){size_t a=rel.find('"',rel.find(':',v)+1),b=a==std::string::npos?a:rel.find('"',a+1);if(b!=std::string::npos)ver=rel.substr(a+1,b-a-1);}
  if(ver.empty()||coop_release_rank("V"+ver)!=rank){fail("The package inside is V"+(ver.empty()?std::string("?"):ver)+", not "+tag+". Nothing was changed.");return;}
  if(!write_text(work+L"\\apply-update.ps1",k_apply)){fail("Could not write the installer step. Nothing was changed.");return;}
  {std::lock_guard<std::mutex> l(mx);version_to=ver;}
  prog=0.95f;st=READY;set_msg("Ready. The game will close for a moment, finish installing and open again.");
 }catch(...){fail("Update failed. Nothing was changed.");}
}
void start(const std::string &url,long long size,const std::string &tag,int rank){
 int s=st.load();if(s==DOWNLOADING||s==PREPARING||s==INSTALLING||s==READY)return;
 prog=0;st=DOWNLOADING;set_msg("Connecting to GitHub...");
 std::thread(worker,url,size,tag,rank).detach();
}
bool install_and_restart(){
 if(st.load()!=READY)return false;
 std::wstring dir=game_dir(),work=dir+L"\\LA-Online-update",pkg=find_package(work+L"\\package",2);std::string ver;{std::lock_guard<std::mutex> l(mx);ver=version_to;}
 wchar_t pid[16];swprintf(pid,16,L"%lu",(unsigned long)GetCurrentProcessId());
 std::wstring cmd=L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \""+work+L"\\apply-update.ps1\" -Game \""+dir+L"\" -Package \""+pkg+L"\" -GamePid "+pid+L" -Version "+wide(ver);
 STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
 if(!CreateProcessW(NULL,&cmd[0],NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,dir.c_str(),&si,&pi)){fail("Could not start the installer step. Nothing was changed.");return false;}
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 st=INSTALLING;prog=0.96f;set_msg("Installing... the game reopens by itself.");return true;
}
std::string just_updated(){
 static bool read=false;static std::string v;if(read)return v;read=true;
 std::wstring p=game_dir()+L"\\LA-Online-update\\updated.txt";v=read_small(p);while(!v.empty()&&(v.back()=='\n'||v.back()=='\r'||v.back()==' '))v.pop_back();
 if(!v.empty())DeleteFileW(p.c_str());
 return v;
}
#else
void start(const std::string &,long long,const std::string &,int){fail("Update Now works on Windows. On this system, download the release from GitHub.");}
bool install_and_restart(){return false;}
std::string just_updated(){return {};}
#endif

// Patch notes as posted on GitHub (Markdown), drawn as simple wrapped text: headings, bullets, quotes.
// Emoji and symbol characters (GitHub notes often use them) are not in the game's fonts and would show as "?".
static std::string strip_emoji(const std::string &s){
 std::string o;o.reserve(s.size());
 for(size_t i=0;i<s.size();){unsigned char c=(unsigned char)s[i];
  if(c>=0xF0){i+=4;continue;}                                   // 4-byte UTF-8: emoji
  if(c>=0xE0&&i+2<s.size()){unsigned cp=((c&15)<<12)|((s[i+1]&63)<<6)|(s[i+2]&63);
   if((cp>=0x2600&&cp<=0x27BF)||cp==0xFE0F||cp==0x200D||(cp>=0x2B00&&cp<=0x2BFF)){i+=3;continue;}
   const char *plain=cp==0x2014||cp==0x2013||cp==0x2212?"-":cp==0x2018||cp==0x2019?"'":cp==0x201C||cp==0x201D?"\"":cp==0x2026?"...":cp==0x2192?"->":cp==0x2022?"-":nullptr;
   if(plain){o+=plain;i+=3;continue;}}
  o+=s[i++];}
 return o;
}
void draw_notes(const std::string &md_in,float scale){
 std::string md=strip_emoji(md_in);
 size_t cut=md.find("LAONLINE_COMPATIBILITY_V1");std::string text=md.substr(0,cut);
 bool fence=false;size_t i=0;
 while(i<text.size()){
  size_t e=text.find('\n',i);std::string line=text.substr(i,e==std::string::npos?std::string::npos:e-i);i=e==std::string::npos?text.size():e+1;
  while(!line.empty()&&(line.back()=='\r'||line.back()==' '))line.pop_back();
  if(line.rfind("```",0)==0){fence=!fence;continue;}
  std::string out;for(size_t k=0;k<line.size();k++){  // strip **, __, `, and [text](link) -> text
   if((line[k]=='*'&&k+1<line.size()&&line[k+1]=='*')||(line[k]=='_'&&k+1<line.size()&&line[k+1]=='_')){k++;continue;}
   if(line[k]=='`')continue;
   if(line[k]=='['){size_t close=line.find("](",k);size_t end=close==std::string::npos?close:line.find(')',close);if(end!=std::string::npos){out+=line.substr(k+1,close-k-1);k=end;continue;}}
   if(line[k]=='<'){size_t end=line.find('>',k);if(end!=std::string::npos&&end-k<40&&(line.compare(k,4,"<br>")==0||line.compare(k,2,"</")==0||isalpha((unsigned char)line[k+1]))){k=end;continue;}}
   out+=line[k];}
  size_t lead=out.find_first_not_of(' ');if(lead==std::string::npos){ImGui::Dummy(ImVec2(1,4*scale));continue;}
  std::string body=out.substr(lead);
  if(fence){ImGui::TextDisabled("%s",body.c_str());continue;}
  if(body=="---"||body=="***"){ImGui::Separator();continue;}
  if(body[0]=='#'){size_t h=body.find_first_not_of('#');std::string t=h==std::string::npos?"":body.substr(h);size_t s=t.find_first_not_of(' ');t=s==std::string::npos?"":t.substr(s);
   ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.55f,.86f,1,1));ImGui::SetWindowFontScale(h<=2?1.18f:1.06f);ImGui::TextWrapped("%s",t.c_str());ImGui::SetWindowFontScale(1);ImGui::PopStyleColor();continue;}
  if(body[0]=='>'){std::string t=body.substr(1);ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.84f,.45f,1));ImGui::TextWrapped("%s",t.c_str());ImGui::PopStyleColor();continue;}
  if((body[0]=='-'||body[0]=='*'||body[0]=='+')&&body.size()>1&&body[1]==' '){ImGui::Indent(lead>0?28*scale:12*scale);ImGui::Bullet();ImGui::SameLine();ImGui::TextWrapped("%s",body.c_str()+2);ImGui::Unindent(lead>0?28*scale:12*scale);continue;}
  ImGui::TextWrapped("%s",body.c_str());
 }
}
}
