#include "../runtime/ui/laonline_seed_share.h"
#include <assert.h>
#include <stdio.h>
extern "C" int laonline_seed_identity_folder(const char *,unsigned char h[32]){for(int i=0;i<32;i++)h[i]=1;return 1;}
int main(){
 for(auto base:{"beta_5.3","beta_5.4"}){LaoSeedRecipe a,b;a.baseline=base;a.generator_id=std::string("LASRecomp-")+base+"+LAO-community3";a.id="NDY8C-EX7R4-30002-6C8AZ-GF000-0003B";a.hash=std::string(64,'a');a.name="Existing Seed";assert(LaoSeedRecipe::decode(a.encode(),b));assert(b.baseline==a.baseline&&b.generator_id==a.generator_id&&b.hash==a.hash);auto bad=a.encode();bad.replace(bad.find(base),8,"beta_9.9");assert(!LaoSeedRecipe::decode(bad,b));}
 puts("PASS native recipe reader: legacy5.3 and new5.4 exact roundtrip; mismatched provenance rejected.");
}
