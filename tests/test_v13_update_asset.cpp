// V13 Update Now: which file of a GitHub release is downloaded.
#include "../runtime/ui/release_updates.h"
#include <stdio.h>
using namespace lao_updates;
static std::string asset(std::string name,long long size,std::string state="uploaded",std::string repo="LegendarySavageGamer/Link-s-Awakening-Recomp-Online"){
 return "{\"name\":\""+name+"\",\"state\":\""+state+"\",\"size\":"+std::to_string(size)+",\"browser_download_url\":\"https://github.com/"+repo+"/releases/download/V13/"+name+"\"}";}
static std::string rel(std::string assets){return "[{\"draft\":false,\"tag_name\":\"V13\",\"html_url\":\"https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases/tag/V13\",\"assets\":["+assets+"],\"body\":\"## New\\n- Join in progress\"}]";}
static int fails;static void check(const char *n,bool ok){printf("%-62s %s\n",n,ok?"PASS":"FAIL");fails+=!ok;}
int main(){
 auto r=newest(rel(asset("LA-Online-BETA-V13-BUILD-KIT.zip",120000000)+","+asset("LA-Online-BETA-V13.zip",70000000)+","+asset("LA-Online-V13-FULL-SOURCE.zip",35000000)),true);
 check("player package picked over build kit / source",r.asset.name=="LA-Online-BETA-V13.zip"&&r.asset.size==70000000);
 check("patch notes read from the release",r.body.find("Join in progress")!=std::string::npos);
 check("only a build kit attached: nothing to auto-install",newest(rel(asset("LA-Online-BETA-V13-BUILD-KIT.zip",120000000)),true).asset.url.empty());
 check("file from another repository refused",newest(rel(asset("LA-Online-BETA-V13.zip",70000000,"uploaded","someone/else")),true).asset.url.empty());
 check("upload not finished refused",newest(rel(asset("LA-Online-BETA-V13.zip",70000000,"starter")),true).asset.url.empty());
 check("tiny / empty file refused",newest(rel(asset("LA-Online-BETA-V13.zip",10)),true).asset.url.empty());
 check("V13 ranks above V12.5",r.rank==13000&&coop_release_rank("V12.5")==12005);
 { // V13.1 test release: what UPDATE NOW sees from V13
  std::string a=asset("LA-Online-BETA-V13.1.zip",71000000),b="[{\"draft\":false,\"tag_name\":\"V13.1\",\"html_url\":\"https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases/tag/V13.1\",\"assets\":["+a+"],\"body\":\"Hello, this is a test\"},"+rel(asset("LA-Online-BETA-V13.zip",70000000)).substr(1);
  auto n=newest(b,true);
  check("V13.1 release is the newest, its zip is picked",n.tag=="V13.1"&&n.rank==13001&&n.asset.name=="LA-Online-BETA-V13.1.zip");
  check("V13.1 is newer than V13 (shows UPDATE NOW on V13)",n.rank>13000);
  check("package RELEASE.json version 13.1 matches tag V13.1",coop_release_rank("V13.1")==n.rank);
  check("V14 later ranks above V13.1",coop_release_rank("V14")>n.rank&&coop_release_rank("V13.2")>n.rank);
 }
 return fails?1:0;
}
