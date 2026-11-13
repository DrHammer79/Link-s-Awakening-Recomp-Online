#include "laonline_report_send.h"
#include "laonline_build.h"
#define LAO_RELEASE_LABEL_SHORT "V" LAO_MOD_VERSION
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#endif
static std::mutex report_mx;
static std::string endpoint;
static std::atomic<int> status(0);
extern "C" void laonline_report_configure(const char *s){
 std::lock_guard<std::mutex> lock(report_mx);
 endpoint=s&&strlen(s)>8&&strlen(s)<512&&!strncmp(s,"https://",8)&&!strpbrk(s,"\r\n\t @#")&&!strstr(s,"/api/webhooks/")?s:"";
}
/* V12.5: Report A Bug works from the start screen too. V10-V12 only learned the address when an online session
 * started (laonline.c load_config), so from the launcher or offline it always said "not configured". The address is
 * now read from laonline.ini on first use, falling back to the project's own report intake (the installer default).
 * It is a public HTTPS intake: the Discord webhook stays on the server (relay/report_backend.py, server env only). */
#define LAO_DEFAULT_REPORT_URL "https://heroes-of-twilight.duckdns.org/laonline/report"
static void endpoint_from_ini(void){  /* caller holds report_mx */
 if(!endpoint.empty())return;
 std::string found;FILE *f=fopen("laonline.ini","rb");
 if(f){char line[768];while(fgets(line,sizeof line,f)){char *p=line;while(*p==' '||*p=='\t')p++;
   if(strncmp(p,"report_url",10))continue;p+=10;while(*p==' '||*p=='\t')p++;if(*p!='=')continue;p++;while(*p==' '||*p=='\t')p++;
   char *end=p+strcspn(p,";\r\n");while(end>p&&(end[-1]==' '||end[-1]=='\t'))end--;found.assign(p,end-p);}
  fclose(f);}
 const char *s=found.empty()?LAO_DEFAULT_REPORT_URL:found.c_str();
 endpoint=strlen(s)>8&&strlen(s)<512&&!strncmp(s,"https://",8)&&!strpbrk(s,"\r\n\t @#")&&!strstr(s,"/api/webhooks/")?s:LAO_DEFAULT_REPORT_URL;
}
extern "C" int laonline_report_endpoint(char *out,size_t cap){
 std::lock_guard<std::mutex> lock(report_mx);endpoint_from_ini();if(out&&cap)snprintf(out,cap,"%s",endpoint.c_str());return !endpoint.empty();
}
extern "C" int laonline_report_status(void){return status.load();}
static std::string last_message;
extern "C" void laonline_report_message(char *out,size_t cap){std::lock_guard<std::mutex> lock(report_mx);if(out&&cap)snprintf(out,cap,"%s",last_message.c_str());}
static void set_message(const std::string &m){std::lock_guard<std::mutex> lock(report_mx);last_message=m;}
/* V12.5: reports go through the relay the game already talks to (laonline.ini "server", port 47620, opcode 0x34).
 * The relay forwards them to Discord with the webhook from its own environment. The old HTTPS intake below is only a
 * fallback: it needed a reverse proxy nobody had set up, which is why reports never arrived. */
static std::string relay_from_ini(void){
 std::string found;FILE *f=fopen("laonline.ini","rb");
 if(f){char line[768];while(fgets(line,sizeof line,f)){char *p=line;while(*p==' '||*p=='\t')p++;
   if(strncmp(p,"server",6))continue;p+=6;while(*p==' '||*p=='\t')p++;if(*p!='=')continue;p++;while(*p==' '||*p=='\t')p++;
   char *end=p+strcspn(p,";\r\n");while(end>p&&(end[-1]==' '||end[-1]=='\t'))end--;found.assign(p,end-p);}
  fclose(f);}
 return found;
}
#ifdef _WIN32
typedef SOCKET ReportSocket;
#define REPORT_BAD INVALID_SOCKET
#define report_close closesocket
#else
typedef int ReportSocket;
#define REPORT_BAD (-1)
#define report_close close
#endif
static bool report_io(ReportSocket s,char *data,size_t n,bool sending){while(n){int k=sending?send(s,data,(int)(n>65536?65536:n),0):recv(s,data,(int)(n>65536?65536:n),0);if(k<=0)return false;data+=k;n-=(size_t)k;}return true;}
/* V13.5: screenshot / video attachment (kept until it was sent or removed) */
#define LAO_ATTACH_MAX (9ll*1024*1024)
static std::string attach_path,attach_name;static long long attach_size;
static int attach_kind(const unsigned char *h,size_t n){  /* by content, not by file name: 1 png 2 jpg 3 mp4/mov 4 webm */
 if(n>=8&&!memcmp(h,"\x89PNG\r\n\x1a\n",8))return 1;
 if(n>=3&&h[0]==0xFF&&h[1]==0xD8&&h[2]==0xFF)return 2;
 if(n>=12&&!memcmp(h+4,"ftyp",4))return 3;
 if(n>=4&&h[0]==0x1A&&h[1]==0x45&&h[2]==0xDF&&h[3]==0xA3)return 4;
 return 0;
}
extern "C" int laonline_report_attach(const char *path,char *why,size_t cap){
 std::lock_guard<std::mutex> lock(report_mx);
 auto say=[&](const char *m){if(why&&cap)snprintf(why,cap,"%s",m);};
 if(!path||!path[0]){attach_path.clear();attach_name.clear();attach_size=0;say("");return 1;}
 FILE *f=fopen(path,"rb");if(!f){say("That file can't be opened.");return 0;}
 unsigned char head[16]={0};size_t got=fread(head,1,sizeof head,f);fseek(f,0,SEEK_END);long long size=ftell(f);fclose(f);
 if(size<=0){say("That file is empty.");return 0;}
 if(size>LAO_ATTACH_MAX){say("That file is bigger than 9 MB (Discord's limit). Trim the video or use a screenshot.");return 0;}
 if(!attach_kind(head,got)){say("Only pictures (PNG, JPG) and videos (MP4, MOV, WEBM) can be attached.");return 0;}
 attach_path=path;std::string n=path;size_t slash=n.find_last_of("/\\");attach_name=slash==std::string::npos?n:n.substr(slash+1);attach_size=size;say("");return 1;
}
extern "C" int laonline_report_attachment(char *name,size_t cap,long long *bytes){
 std::lock_guard<std::mutex> lock(report_mx);if(attach_path.empty())return 0;
 if(name&&cap)snprintf(name,cap,"%s",attach_name.c_str());if(bytes)*bytes=attach_size;return 1;
}
/* 1 delivered, 0 the relay answered "no" (message says why), -1 could not reach / talk to the relay.
 * V13.5: with an attachment the header is version 2: size, kind, attachment size; the file follows the report. */
static int relay_post(const std::string &address,const std::string &body,std::string &message,const std::string &attachment=std::string(),int kind=0){
 if(address.empty()){message="No server is set in laonline.ini.";return -1;}
 std::string host=address,port="47620";auto split=host.rfind(':');if(split!=std::string::npos){port=host.substr(split+1);host.resize(split);}
#ifdef _WIN32
 WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)){message="Networking is unavailable.";return -1;}
 struct Cleanup{~Cleanup(){WSACleanup();}} cleanup;
#endif
 addrinfo hint={},*found=nullptr;hint.ai_family=AF_INET;hint.ai_socktype=SOCK_STREAM;
 if(getaddrinfo(host.c_str(),port.c_str(),&hint,&found)||!found){message="Cannot find the server "+host+".";return -1;}
 ReportSocket s=socket(found->ai_family,found->ai_socktype,found->ai_protocol);if(s==REPORT_BAD){freeaddrinfo(found);message="Cannot open a connection.";return -1;}
#ifdef _WIN32
 DWORD timeout=20000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof timeout);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof timeout);
#else
 timeval timeout={20,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
#endif
 int failed=connect(s,found->ai_addr,(int)found->ai_addrlen);freeaddrinfo(found);
 if(failed){report_close(s);message="Cannot connect to the server.";return -1;}
 uint32_t n=(uint32_t)body.size();char head[8]={6,0,0x34,1,(char)(n&255),(char)((n>>8)&255),(char)((n>>16)&255),(char)(n>>24)};
 std::string packet;
 if(attachment.empty())packet.assign(head,8);
 else{uint32_t a=(uint32_t)attachment.size();char h2[13]={11,0,0x34,2,(char)(n&255),(char)((n>>8)&255),(char)((n>>16)&255),(char)(n>>24),(char)kind,(char)(a&255),(char)((a>>8)&255),(char)((a>>16)&255),(char)(a>>24)};packet.assign(h2,13);}
 packet+=body;packet+=attachment;int result=-1;unsigned char reply[3];
 if(!report_io(s,&packet[0],packet.size(),true))message="The connection dropped while sending.";
 else if(!report_io(s,(char*)reply,3,false)||reply[2]!=0xb4)message="This server is older than V12.5 and can't take reports yet. Update the relay (RELAY-V12).";
 else{size_t len=reply[0]|(reply[1]<<8);std::string rest(len>1?len-1:0,'\0');
  if(len<3||len>1024||!report_io(s,&rest[0],rest.size(),false))message="Incomplete answer from the server.";
  else{size_t m=(unsigned char)rest[1];message=rest.size()>=2+m?rest.substr(2,m):"";result=rest[0]==1?1:0;}}
 report_close(s);return result;
}
/* V15.1 SEND LOGS TO DEVELOPER (runtime/ui/laonline_logs.mm): the zip of every log goes through the same relay (opcode
 * 0x35) to the developer's logs webhook on the server. 1 sent, 0 the server said no (message says why), -1 no server. */
extern "C" int laonline_logs_upload(const char *zip_path,const char *player,const char *code,char *msg,size_t cap){
 auto say=[&](const std::string &m){if(msg&&cap)snprintf(msg,cap,"%s",m.c_str());};
 std::string zip;{FILE *f=fopen(zip_path,"rb");if(!f){say("The zip could not be opened.");return -1;}
  fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);if(n<22||n>(long)(9.5*1024*1024)){fclose(f);say("The zip is too big to send (over 9.5 MB).");return 0;}
  zip.resize((size_t)n);size_t got=fread(&zip[0],1,zip.size(),f);fclose(f);if(got!=zip.size()){say("The zip could not be read.");return -1;}}
 std::string address=relay_from_ini();if(address.empty()){say("No server is set in laonline.ini.");return -1;}
 std::string host=address,port="47620";auto split=host.rfind(':');if(split!=std::string::npos){port=host.substr(split+1);host.resize(split);}
#ifdef _WIN32
 WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)){say("Networking is unavailable.");return -1;}
 struct Cleanup{~Cleanup(){WSACleanup();}} cleanup;
#endif
 addrinfo hint={},*found=nullptr;hint.ai_family=AF_INET;hint.ai_socktype=SOCK_STREAM;
 if(getaddrinfo(host.c_str(),port.c_str(),&hint,&found)||!found){say("Cannot find the server "+host+".");return -1;}
 ReportSocket s=socket(found->ai_family,found->ai_socktype,found->ai_protocol);if(s==REPORT_BAD){freeaddrinfo(found);say("Cannot open a connection.");return -1;}
#ifdef _WIN32
 DWORD timeout=180000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof timeout);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof timeout);
#else
 timeval timeout={180,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
#endif
 int failed=connect(s,found->ai_addr,(int)found->ai_addrlen);freeaddrinfo(found);
 if(failed){report_close(s);say("Cannot connect to the server.");return -1;}
 auto str=[](const char *t,size_t max){std::string v=t?t:"";if(v.size()>max)v.resize(max);return std::string(1,(char)v.size())+v;};
 uint32_t n=(uint32_t)zip.size();
 std::string body;body+=(char)0x35;body+=(char)1;for(int i=0;i<4;i++)body+=(char)((n>>(8*i))&255);
 body+=str(player,16);body+=str(code,9);body+=str(LAO_RELEASE_LABEL_SHORT,48);
 std::string packet;packet+=(char)(body.size()&255);packet+=(char)((body.size()>>8)&255);packet+=body;packet+=zip;
 int result=-1;std::string message;unsigned char reply[3];
 if(!report_io(s,&packet[0],packet.size(),true))message="The connection dropped while sending.";
 else if(!report_io(s,(char*)reply,3,false)||reply[2]!=0xb5)message="Your server is older and can't take logs yet (the host updates it with UPDATE-SERVER).";
 else{size_t len=reply[0]|(reply[1]<<8);std::string rest(len>1?len-1:0,'\0');
  if(len<3||len>1024||!report_io(s,&rest[0],rest.size(),false))message="Incomplete answer from the server.";
  else{size_t m=(unsigned char)rest[1];message=rest.size()>=2+m?rest.substr(2,m):"";result=rest[0]==1?1:0;}}
 report_close(s);say(message);return result;
}
extern "C" void laonline_report_clear_status(void){int old=status.load();while(old!=1&&!status.compare_exchange_weak(old,0)){} }
#ifdef _WIN32
static bool post(const std::string &address,const std::string &body){
 wchar_t url[512];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,address.c_str(),-1,url,512))return false;
 URL_COMPONENTS u={};u.dwStructSize=sizeof u;u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=(DWORD)-1;
 if(!WinHttpCrackUrl(url,0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS||u.dwUserNameLength||u.dwPasswordLength)return false;
 std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);
 if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
 if(path.find(L'#')!=std::wstring::npos)return false;
 HINTERNET session=WinHttpOpen(L"LA-Online-HF2/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
 if(!session)return false;
 WinHttpSetTimeouts(session,5000,5000,5000,10000);
 HINTERNET connection=WinHttpConnect(session,host.c_str(),u.nPort,0),request=NULL;
 if(connection)request=WinHttpOpenRequest(connection,L"POST",path.empty()?L"/":path.c_str(),NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
 bool ok=false;
 if(request){
  DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  bool configured=WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof redirects)&&WinHttpSetOption(request,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof auth);
  if(configured&&WinHttpSendRequest(request,L"Content-Type: application/json\r\n",(DWORD)-1,(void *)body.data(),(DWORD)body.size(),(DWORD)body.size(),0)&&WinHttpReceiveResponse(request,NULL)){
   DWORD code=0,size=sizeof code;
   if(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)&&code==200){
    char reply[513]={};DWORD got=0;std::string response;
    while(response.size()<512 && WinHttpReadData(request,reply,(DWORD)(512-response.size()),&got)&&got)response.append(reply,got);
    // The backend returns this only after Discord confirms a created message.
    ok=response=="{\"delivered\":true}";
   }
  }
  WinHttpCloseHandle(request);
 }
 if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return ok;
}
#endif
extern "C" int laonline_report_send(const char *json){
 if(!json||strlen(json)>60000||status.load()==1)return 0;
 std::string address;{std::lock_guard<std::mutex> lock(report_mx);endpoint_from_ini();address=endpoint;}
 if(address.empty())return 0;
 int expected=status.load();do{if(expected==1)return 0;}while(!status.compare_exchange_weak(expected,1));
 std::string body=json,relay=relay_from_ini();set_message("");
 try{std::thread([address,relay,body](){
   std::string message,file;int kind=0;
   {std::string path;{std::lock_guard<std::mutex> lock(report_mx);path=attach_path;}
    if(!path.empty()){FILE *f=fopen(path.c_str(),"rb");if(f){file.resize(LAO_ATTACH_MAX+1);size_t got=fread(&file[0],1,file.size(),f);fclose(f);file.resize(got);
      kind=attach_kind((const unsigned char*)file.data(),file.size());if(!kind||(long long)file.size()>LAO_ATTACH_MAX){file.clear();kind=0;}}}}
   int r=relay_post(relay,body,message,file,kind);
   bool dropped=false;
   if(r==0&&!file.empty()&&message.find("rejected")!=std::string::npos){dropped=true;r=relay_post(relay,body,message);}  /* older server: send without it */
   if(r==1){{std::lock_guard<std::mutex> lock(report_mx);attach_path.clear();attach_name.clear();attach_size=0;}
    set_message(dropped?"Report delivered (your server is older, so the picture/video was not sent - the host can update server.py).":"Report delivered.");status.store(2);return;}
#ifdef _WIN32
   if(post(address,body)){set_message("Report delivered.");status.store(2);return;}  // old HTTPS intake, if a host set one up
#endif
   set_message(message.empty()?"The report could not be delivered.":message);status.store(-1);
  }).detach();}
 catch(...){status=-1;return 0;}
 return 1;
}
