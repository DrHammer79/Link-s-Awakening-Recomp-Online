#include "../runtime/ui/release_updates.h"
#include <assert.h>
#include <stdio.h>
#include <fstream>
#include <sstream>
using namespace lao_updates;
static std::string release(std::string tag,std::string body,bool coop=false,bool draft=false){
 auto esc=[](std::string x){std::string o;for(char c:x){if(c=='"'||c=='\\')o+='\\';o+=c;}return o;};
 return std::string("{\"draft\":")+(draft?"true":"false")+",\"tag_name\":\""+tag+"\",\"html_url\":\"https://github.com/"+(coop?"LegendarySavageGamer/Link-s-Awakening-Recomp-Online":"peiikai/LASRecomp")+"/releases/tag/"+tag+"\",\"assets\":[{\"tag_name\":\"beta_999\"}],\"body\":\""+esc(body)+"\"}";
}
int main(){
 assert(newest("["+release("beta_5.4","")+","+release("beta_5.5","")+"]",false).tag=="beta_5.5");
 assert(newest("["+release("beta_5.4","")+","+release("beta_5.5","",false,true)+"]",false).tag=="beta_5.4");
 assert(newest("["+release("BETA-V11","",true)+"]",true).rank>LAO_COOP_RANK);
 assert(newest("["+release("beta_5.5","",true)+"]",true).rank<0);
 auto metadata=[](std::string status,std::string tag="BETA-V11"){return "LAONLINE_COMPATIBILITY_V1 {\"las_tag\":\"beta_5.5\",\"coop_tag\":\""+tag+"\",\"status\":\""+status+"\"}";};
 assert(verified_coop_for("["+release("BETA-V11",metadata("verified"),true)+"]","beta_5.5")=="BETA-V11");
 for(auto status:{"unknown","unsupported","VERIFIED",""})assert(verified_coop_for("["+release("BETA-V11",metadata(status),true)+"]","beta_5.5").empty());
 assert(verified_coop_for("["+release("BETA-V11",metadata("verified","BETA-V12"),true)+"]","beta_5.5").empty());
 assert(verified_coop_for("["+release("BETA-V11",metadata("verified"),true,true)+"]","beta_5.5").empty());
 assert(verified_coop_for("["+release("BETA-V11",metadata("verified"),true)+"]","beta_5.6").empty());
 assert(verified_coop_for("["+release("BETA-V11","Supports the new LAS!",true)+"]","beta_5.5").empty());
 assert(field("{\"assets\":[{\"tag_name\":\"beta_999\"}],\"tag_name\":\"beta_5.4\"}","tag_name")=="beta_5.4");
 assert(!published("{\"assets\":[{\"draft\":false}]}"));
 assert(objects("[{\"body\":\"unfinished").empty());assert(newest("[]",false).rank<0);
 puts("PASS real-release model: separate versions, numeric order, drafts and malformed responses rejected, nested fields ignored, compatibility requires exact published developer declaration; unknown/unsupported never called verified.");
}
