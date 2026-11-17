// Content identity for co-op: independent of folder names and local save files.
#include "cache_sha256.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
extern "C" const char *rando_seed_dir(void);
extern "C" int laonline_seed_identity_folder(const char *dir,unsigned char out[32]) {
 try {
  CacheSha256 hash; const char schema[]="LAO15-1.0.0-inventory-v1";
  hash.add(schema,sizeof schema);
  if(dir) {
   namespace fs=std::filesystem;
   fs::path root(dir); std::vector<std::string> names;
   for(auto sub:{"romfs","exefs"}) {
    fs::path base=root/sub;
    if(!fs::is_directory(base)) return 0;
    for(auto const &entry:fs::recursive_directory_iterator(base))
     if(entry.is_regular_file()) names.push_back(entry.path().lexically_relative(root).generic_string());
   }
   if(names.empty() || !fs::is_regular_file(root/"settings.json")) return 0;
   names.push_back("settings.json");
   // Runtime metadata can change item indices/logic without changing a ROMFS file.
   for(auto const &entry:fs::directory_iterator(root)) {
    auto ext=entry.path().extension();
    auto file=entry.path().filename();
    if(entry.is_regular_file() && (ext==".tsv" || file=="flags_ext" || file=="logic.json" ||
        file=="tracker.json" || file=="hint_steps.json" || file=="spheres.json"))
     names.push_back(entry.path().filename().generic_string());
   }
   std::sort(names.begin(),names.end());
   char block[65536];
   for(auto const &name:names) {
    auto length=fs::file_size(root/name);
    hash.add(name.c_str(),name.size()+1);unsigned char size_le[8];for(int i=0;i<8;i++)size_le[i]=(unsigned char)(length>>(8*i));hash.add(size_le,sizeof size_le);
    std::ifstream f(root/name,std::ios::binary); if(!f) return 0;
    while(f) {f.read(block,sizeof block);hash.add(block,(size_t)f.gcount());}
    if(!f.eof()) return 0;
   }
  } else {const char vanilla[]="vanilla";hash.add(vanilla,sizeof vanilla);}
  auto hex=hash.finish();
  for(int i=0;i<32;i++) out[i]=(unsigned char)std::stoul(hex.substr(i*2,2),nullptr,16);
  return 1;
 } catch(const std::exception &e) {fprintf(stderr,"[laonline] V15 seed identity: %s\n",e.what());return 0;}
}



extern "C" int laonline_seed_identity(unsigned char out[32]){return laonline_seed_identity_folder(rando_seed_dir(),out);}
