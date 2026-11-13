// A reconstruction recipe, never ROMFS, saves, credentials or game assets.
// The upstream ID encodes the random value and all supported generator options.
#pragma once
#include <string>
#include <fstream>
#include <filesystem>
extern "C" int laonline_seed_identity_folder(const char *, unsigned char[32]);
struct LaoSeedRecipe {
    std::string id, hash, name;
    std::string baseline="beta_5.4",generator_id="LASRecomp-beta_5.4+LAO-community3";
    static constexpr const char *generator="LASRecomp-beta_5.4+LAO-community3";
    static bool hex(const std::string &s) {
        return s.size()==64 && s.find_first_not_of("0123456789abcdef")==std::string::npos;
    }
    static bool safe_name(const std::string &s) {
        if(s.empty()||s.size()>90||s=="."||s=="..")return false;
        for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
              (c>='0'&&c<='9')||c==' '||c=='_'||c=='-'||c=='('||c==')'))return false;
        return true;
    }
    // v2 records the human-readable seed name. This is a *recipe*, not world assets.
    std::string encode() const {
        return "LAO-SEED-2\nbaseline="+baseline+"\ngame=1.0.0\ngenerator="+generator_id+
               "\nseed_id="+id+"\nworld_sha256="+hash+"\ndisplay_name="+name+"\n";
    }
    static bool decode(const std::string &s,LaoSeedRecipe &out) {
        if(s.size()>2048 || s.find('\0')!=std::string::npos)return false;
        std::string text;for(char c:s)if(c!='\r')text+=c;
        const bool legacy=text.find("\nbaseline=beta_5.3\n")!=std::string::npos;
        const std::string baseline=legacy?"beta_5.3":"beta_5.4";
        const std::string gen=legacy?"LASRecomp-beta_5.3+LAO-community3":generator;
        const std::string base="\nbaseline="+baseline+"\ngame=1.0.0\ngenerator="+gen+"\nseed_id=";
        bool v2=text.rfind("LAO-SEED-2"+base,0)==0;
        const std::string prefix=(v2?"LAO-SEED-2":"LAO-SEED-1")+base;
        if(text.rfind(prefix,0)!=0)return false;
        size_t end=text.find('\n',prefix.size());if(end==std::string::npos)return false;
        LaoSeedRecipe trial;trial.baseline=baseline;trial.generator_id=gen;trial.id=text.substr(prefix.size(),end-prefix.size());
        const std::string marker="\nworld_sha256=";
        if(text.compare(end,marker.size(),marker)!=0)return false;
        size_t pos=end+marker.size();
        if(pos>text.size()||text.size()-pos<64)return false;
        trial.hash=text.substr(pos,64);
        if(!hex(trial.hash)||trial.id.size()>95||trial.id.empty()||
           trial.id.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZ-")!=std::string::npos)return false;
        pos+=64;
        if(v2){
            const std::string label="\ndisplay_name=";
            if(text.compare(pos,label.size(),label)!=0)return false;
            pos+=label.size();
            if(text.empty()||text.back()!='\n')return false;
            trial.name=text.substr(pos,text.size()-pos-1);
            if(!safe_name(trial.name))return false;
        }else if(text.substr(pos)!="\n")return false;
        out=trial;return true;
    }
};
static std::string lao_seed_display_name(const std::filesystem::path &root){
    std::string name;std::ifstream label(root/"seed_display_name.txt");
    std::getline(label,name);while(!name.empty()&&name.back()=='\r')name.pop_back();
    if(!LaoSeedRecipe::safe_name(name))name=root.filename().string();
    return LaoSeedRecipe::safe_name(name)?name:"Seed";
}
static bool lao_seed_recipe(const std::filesystem::path &root,LaoSeedRecipe &out) {
    LaoSeedRecipe trial;std::ifstream f(root/"seed_id.txt");if(!std::getline(f,trial.id))return false;
    while(!trial.id.empty()&&(trial.id.back()=='\r'||trial.id.back()==' '))trial.id.pop_back();
    unsigned char hash[32];auto path=root.string();if(!laonline_seed_identity_folder(path.c_str(),hash))return false;
    const char *hex="0123456789abcdef";for(auto c:hash){trial.hash+=hex[c>>4];trial.hash+=hex[c&15];}
    // Metadata is cosmetic, never part of the authoritative full-world hash.
    trial.name=lao_seed_display_name(root);
    std::ifstream marker(root/"generator.version");std::string base,gen;
    if(marker){std::getline(marker,base);std::getline(marker,gen);if(!base.empty()&&base.back()=='\r')base.pop_back();if(!gen.empty()&&gen.back()=='\r')gen.pop_back();
        if(!((base=="beta_5.3"&&gen=="LASRecomp-beta_5.3+LAO-community3")||(base=="beta_5.4"&&gen==LaoSeedRecipe::generator)))return false;
        trial.baseline=base;trial.generator_id=gen;
    }else{trial.baseline="beta_5.3";trial.generator_id="LASRecomp-beta_5.3+LAO-community3";}
    out=trial;return true;
}
static bool lao_write_recipe(const std::filesystem::path &destination,const LaoSeedRecipe &recipe) {
    // No truncation of an existing file. The dialog explains that a new filename is required.
    namespace fs=std::filesystem;std::error_code ec;
    if(fs::exists(destination,ec)||ec)return false;
    fs::path temporary=destination;temporary+=".lao-writing";
    if(fs::exists(temporary,ec)||ec)return false;
    if(!LaoSeedRecipe::safe_name(recipe.name)||!LaoSeedRecipe::hex(recipe.hash))return false;
    std::ofstream f(temporary,std::ios::binary);if(!f)return false;
    auto data=recipe.encode();f.write(data.data(),data.size());f.close();
    if(!f){std::error_code ignored;fs::remove(temporary,ignored);return false;}
    bool copied=fs::copy_file(temporary,destination,fs::copy_options::none,ec);
    {std::error_code ignored;fs::remove(temporary,ignored);}
    return copied&&!ec;
}
