#include <assert.h>
#include "../runtime/ui/coop_version.h"
int main(){assert(coop_release_rank("BETA-V7.5")==LAO_COOP_RANK);assert(coop_release_rank("BETA-V7.1")<LAO_COOP_RANK);assert(coop_release_rank("V7")<LAO_COOP_RANK);assert(coop_release_rank("V8")>coop_release_rank("V7.99"));assert(coop_release_rank("V10")>coop_release_rank("V9"));for(auto s:{"V7.","V7.1junk","beta_5.3","V7.1.2"})assert(coop_release_rank(s)==-1);}
