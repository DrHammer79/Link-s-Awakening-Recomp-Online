#pragma once
#include <string>
#include "../laonline_build.h"
// Numbered co-op tags only; upstream LAS versions must never enter this comparison.
static inline int coop_build_number(std::string tag){
 for(char &c:tag)if(c>='a'&&c<='z')c-=32;
 for(const char *prefix:{"CO-OP-BETA-V","BETA-V","V"}){
  std::string p(prefix);if(tag.rfind(p,0)!=0)continue;
  int n=0;if(tag.size()==p.size())return -1;
  for(size_t i=p.size();i<tag.size();i++){if(tag[i]<'0'||tag[i]>'9'||n>100000)return -1;n=n*10+tag[i]-'0';}
  return n>0?n:-1;
 }return -1;
}

// Minor co-op releases rank numerically: V7 < V7.1 < V8 < V10.
static inline int coop_release_rank(const std::string &tag){
 size_t dot=tag.find('.');std::string major=tag.substr(0,dot);int build=coop_build_number(major);if(build<0||build>100000)return -1;
 int revision=0;if(dot!=std::string::npos){if(dot+1==tag.size())return -1;for(size_t i=dot+1;i<tag.size();i++){if(tag[i]<'0'||tag[i]>'9')return -1;revision=revision*10+tag[i]-'0';if(revision>999)return -1;}}
 return build*1000+revision;
}
