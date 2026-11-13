#pragma once
#include "coop_version.h"
#include "update_tags.h"
#include <vector>
#include <cctype>
// Bounded GitHub release reader. Only direct object fields count; fields inside
// assets or release-note strings cannot impersonate tag, draft or URL fields.
namespace lao_updates {
inline size_t key(const std::string &s,const char *name){
 int depth=0;bool quote=false,escape=false;std::string wanted=std::string("\"")+name+"\"";
 for(size_t i=0;i<s.size();i++) {char c=s[i];if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}
  if(c=='"'){if(depth==1&&s.compare(i,wanted.size(),wanted)==0){size_t p=s.find_first_not_of(" \r\n\t",i+wanted.size());if(p!=std::string::npos&&s[p]==':')return s.find_first_not_of(" \r\n\t",p+1);}quote=true;}
  else if(c=='{'||c=='[')depth++;else if(c=='}'||c==']')depth--;
 }return std::string::npos;
}
inline std::string field(const std::string &s,const char *name){
 size_t p=key(s,name);if(p==std::string::npos||s[p]!='"')return {};std::string out;
 for(++p;p<s.size()&&out.size()<1024*1024;p++){char c=s[p];if(c=='"')return out;if((unsigned char)c<32)return {};if(c=='\\'){
  if(++p>=s.size())return {};c=s[p];if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='f')c='\f';else if(c=='u'){
   unsigned cp=0;for(int j=0;j<4;j++){if(++p>=s.size()||!isxdigit((unsigned char)s[p]))return {};char h=s[p];cp=cp*16+(h<='9'?h-'0':tolower(h)-'a'+10);}
   // Supplementary characters in prose are irrelevant to ASCII metadata.
   if(cp>=0xd800&&cp<=0xdfff){out+='?';continue;}
   if(cp<128)out+=(char)cp;else if(cp<2048){out+=(char)(0xc0|(cp>>6));out+=(char)(0x80|(cp&63));}else{out+=(char)(0xe0|(cp>>12));out+=(char)(0x80|((cp>>6)&63));out+=(char)(0x80|(cp&63));}continue;
  }else if(c!='"'&&c!='\\'&&c!='/')return {};
 }out+=c;}return {};
}
inline bool published(const std::string &s){size_t p=key(s,"draft");return p!=std::string::npos&&s.compare(p,5,"false")==0&&(p+5==s.size()||s[p+5]==','||s[p+5]=='}'||isspace((unsigned char)s[p+5]));}
inline std::vector<std::string> objects(const std::string &s){
 std::vector<std::string> out;int depth=0;size_t start=0;bool quote=false,escape=false;
 if(s.size()>2*1024*1024)return out;
 for(size_t i=0;i<s.size();i++){char c=s[i];if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}if(c=='"'){quote=true;continue;}if(c=='{'){if(!depth)start=i;if(++depth>64)return {};}else if(c=='}'){if(--depth==0)out.push_back(s.substr(start,i-start+1));if(depth<0)return {};}}
 if(depth||quote)return {};return out;
}
/* V13 Update Now: the player package attached to a Co-op release (LA-Online-BETA-V13.zip, made by BUILD-V13).
 * Build kits, source and relay zips are never picked; the download URL must be this repository's release download. */
struct Asset{std::string name,url;long long size=-1;};
inline long long number(const std::string &s,const char *name){size_t p=key(s,name);if(p==std::string::npos||!isdigit((unsigned char)s[p]))return -1;long long v=0;while(p<s.size()&&isdigit((unsigned char)s[p])&&v<(1ll<<40))v=v*10+(s[p++]-'0');return v;}
inline Asset player_asset(const std::string &release){
 Asset best;size_t p=key(release,"assets");if(p==std::string::npos||release[p]!='[')return best;
 int depth=0;bool quote=false,escape=false;size_t end=p;
 for(;end<release.size();end++){char c=release[end];if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}if(c=='"')quote=true;else if(c=='['||c=='{')depth++;else if(c==']'||c=='}'){if(--depth==0)break;}}
 if(end>=release.size())return best;
 const std::string prefix="https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases/download/";
 for(const auto &a:objects(release.substr(p+1,end-p-1))){
  std::string name=field(a,"name"),url=field(a,"browser_download_url"),upper=name;for(char &c:upper)c=(char)toupper((unsigned char)c);
  long long size=number(a,"size");
  if(url.rfind(prefix,0)!=0||url.find_first_of("\r\n\t \"")!=std::string::npos||size<100000||size>800ll*1024*1024)continue;
  if(upper.size()<5||upper.compare(upper.size()-4,4,".ZIP")!=0||upper.find("LA-ONLINE")==std::string::npos)continue;
  if(upper.find("BUILD-KIT")!=std::string::npos||upper.find("SOURCE")!=std::string::npos||upper.find("RELAY")!=std::string::npos||upper.find("HANDOFF")!=std::string::npos)continue;
  if(field(a,"state")!="uploaded")continue;
  bool exact=upper.find("LA-ONLINE-BETA-V")!=std::string::npos;
  if(best.url.empty()||exact)best={name,url,size};
  if(exact)break;
 }return best;
}
struct Release{std::string tag,url,body;long long rank=-1;Asset asset;};
inline Release newest(const std::string &body,bool coop){
 Release best;const std::string prefix=coop?"https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases/tag/":"https://github.com/peiikai/LASRecomp/releases/tag/";
 for(const auto &s:objects(body)){if(!published(s))continue;auto tag=field(s,"tag_name"),url=field(s,"html_url");long long rank=coop?coop_release_rank(tag):tag_rank(tag);
  if(rank>best.rank&&url.rfind(prefix,0)==0&&url.find_first_of("\r\n\t ") == std::string::npos)best={tag,url,field(s,"body"),rank,coop?player_asset(s):Asset{}};
 }return best;
}
inline std::string verified_coop_for(const std::string &body,const std::string &las){
 long long best=-1;std::string match;
 for(const auto &s:objects(body)){if(!published(s))continue;auto tag=field(s,"tag_name"),url=field(s,"html_url"),notes=field(s,"body");int rank=coop_release_rank(tag);
  if(rank<0||url.rfind("https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases/tag/",0)!=0)continue;
  const std::string marker="LAONLINE_COMPATIBILITY_V1";size_t at=notes.find(marker);if(at==std::string::npos)continue;auto entries=objects(notes.substr(at+marker.size()));
  for(const auto &entry:entries)if(field(entry,"las_tag")==las&&field(entry,"status")=="verified"&&coop_release_rank(field(entry,"coop_tag"))==rank&&rank>best){best=rank;match=tag;}
 }return match;
}
}
