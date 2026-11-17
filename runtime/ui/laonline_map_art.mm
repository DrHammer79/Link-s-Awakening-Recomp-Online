#include "laonline_map_art.h"
#include "../paths.h"
extern const char *g_romfs_dir;
extern "C" {
#include "../nvn/astc.h"
#include "../nvn/tegra.h"
}
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef __APPLE__
#import <Metal/Metal.h>
extern "C" void *ui_metal_device(void);
#else
extern "C" uint64_t vk_imgui_texture_rgba(const void *,int,int);
#endif
namespace {
struct Bytes {const uint8_t *p=nullptr;size_t n=0;bool has(size_t at,size_t len)const{return at<=n&&len<=n-at;}uint16_t u16(size_t at)const{return has(at,2)?p[at]|p[at+1]<<8:0;}uint32_t u32(size_t at)const{return has(at,4)?p[at]|p[at+1]<<8|p[at+2]<<16|uint32_t(p[at+3])<<24:0;}uint64_t u64(size_t at)const{return u32(at)|(uint64_t(u32(at+4))<<32);}float f32(size_t at)const{uint32_t v=u32(at);float f;memcpy(&f,&v,4);return f;}bool eq(size_t at,const char *s,size_t len)const{return has(at,len)&&!memcmp(p+at,s,len);}};
static Bytes member(Bytes b,const char *wanted){
 if(!b.eq(0,"SARC",4)||!b.eq(0x14,"SFAT",4))return {};
 size_t count=b.u16(0x1a),data=b.u32(0xc),names=0x20+count*16+8;
 if(count>10000||!b.has(0x20,count*16)||!b.eq(names-8,"SFNT",4))return {};
 for(size_t i=0;i<count;i++){size_t e=0x20+i*16,at=names+4*(b.u32(e+4)&0xffffff);size_t len=strlen(wanted);
  if(!b.has(at,len+1)||!b.eq(at,wanted,len)||b.p[at+len])continue;
  size_t start=b.u32(e+8),end=b.u32(e+12);if(end<start||!b.has(data,start)||!b.has(data+start,end-start))return {};
  return {b.p+data+start,end-start};
 }return {};
}
struct Piece {bool valid=false;float x=0,y=0,w=0,h=0;};
struct Atlas {std::vector<uint8_t> rgba;int w=0,h=0;float left=0,top=0,right=0,bottom=0;};
// Verified Game.arc Gm_Map layout: parent N_null_00 scale 1/3, Y +3;
// each L_FldArea_nn is centered, child artwork scale 2/3. Read every size from the layout.
static bool decode(const char *path,Atlas &out){
 FILE *f=fopen(path,"rb");if(!f)return false;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 if(size<32||size>512*1024*1024){fclose(f);return false;}std::vector<uint8_t> archive(size);bool ok=fread(archive.data(),1,size,f)==size;fclose(f);if(!ok)return false;
 Bytes arc{archive.data(),archive.size()},layout=member(arc,"blyt/Gm_Map.bflyt"),tex=member(arc,"timg/__Combined.bntx");
 if(!layout.eq(0,"FLYT",4)||!tex.eq(0,"BNTX",4))return false;
 Piece pieces[23];float ps=0,ty=0;bool parent=false;
 for(size_t p=layout.u16(6);layout.has(p,8);){size_t z=layout.u32(p+4);if(z<8||!layout.has(p,z))return false;
  if(z>=0x54&&layout.has(p+12,24)){
   char name[25]={};memcpy(name,layout.p+p+12,24);
   if(!strcmp(name,"N_null_00")){ps=layout.f32(p+0x44);ty=layout.f32(p+0x30);parent=std::isfinite(ty)&&std::isfinite(ps)&&fabsf(ps-1.f/3)<.00001f&&fabsf(layout.f32(p+0x48)-ps)<.00001f;}
   int i=-1;if(layout.eq(p,"prt1",4)&&sscanf(name,"L_FldArea_%d",&i)==1&&i>=0&&i<23){Piece &q=pieces[i];q={true,layout.f32(p+0x2c),layout.f32(p+0x30),layout.f32(p+0x4c),layout.f32(p+0x50)};if(!std::isfinite(q.x)||!std::isfinite(q.y)||!(q.w>0&&q.w<10000&&q.h>0&&q.h<10000))return false;}
  }p+=z;
 }
 if(!parent)return false;
 out.left=out.bottom=1e9f;out.right=out.top=-1e9f;
 for(auto &q:pieces){if(!q.valid)return false;q.x*=ps;q.y=q.y*ps+ty;q.w*=ps;q.h*=ps;out.left=std::min(out.left,q.x-q.w/2);out.right=std::max(out.right,q.x+q.w/2);out.bottom=std::min(out.bottom,q.y-q.h/2);out.top=std::max(out.top,q.y+q.h/2);}
 float density=1536.f/(out.right-out.left);out.w=1536;out.h=int(ceilf((out.top-out.bottom)*density));if(out.h<1||out.h>2048)return false;out.rgba.assign(out.w*out.h*4,0);
 bool decoded[23]={};size_t count=tex.u32(0x24),infos=tex.u64(0x28);if(count>10000||!tex.has(infos,count*8))return false;
 for(size_t k=0;k<count;k++){
  size_t t=tex.u64(infos+k*8)+16;if(!tex.has(t,104))return false;size_t at=tex.u64(t+80),nl=tex.u16(at);if(nl>128||!tex.has(at+2,nl))return false;
  char name[129]={};memcpy(name,tex.p+at+2,nl);int idx=-1;char expected[32];
  if(sscanf(name,"FldChip_%d",&idx)!=1||idx<0||idx>=23)continue;snprintf(expected,sizeof expected,"FldChip_%02d^_H",idx);if(strcmp(name,expected))continue;
  uint32_t w=tex.u32(t+20),h=tex.u32(t+24),bh=tex.u32(t+36)&7,storage=tex.u32(t+64);size_t ptr=tex.u64(t+96);if(!tex.has(ptr,8))return false;size_t data=tex.u64(ptr);
  if(tex.u32(t+12)!=0x3806||!w||!h||w>4096||h>4096||!tex.has(data,storage))return false;
  FormatInfo format={16,10,10,FMT_COMPRESSED,"ASTC10"};TexLayout tl={w,h,1,1,1,&format,bh,0};if(tegra_total_size(&tl)>storage)return false;
  std::vector<uint8_t> linear(((w+9)/10)*((h+9)/10)*16),rgba(size_t(w)*h*4);
  tegra_deswizzle(tex.p+data,linear.data(),(w+9)/10,(h+9)/10,1,16,bh,0);astc_decode_ldr(linear.data(),w,h,10,10,true,rgba.data());
  const Piece &q=pieces[idx];int x0=int(floorf((q.x-q.w/2-out.left)*density)),y0=int(floorf((out.top-q.y-q.h/2)*density));int dw=int(roundf(q.w*density)),dh=int(roundf(q.h*density));
  for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){int ax=x0+x,ay=y0+y;if(ax<0||ay<0||ax>=out.w||ay>=out.h)continue;int sx=std::min(int(w)-1,int((x+.5f)*w/dw)),sy=std::min(int(h)-1,int((y+.5f)*h/dh));const uint8_t *s=&rgba[(size_t(sy)*w+sx)*4];uint8_t *d=&out.rgba[(size_t(ay)*out.w+ax)*4];unsigned a=s[3],oa=d[3],na=a+oa*(255-a)/255;if(!na)continue;for(int c=0;c<3;c++)d[c]=(s[c]*a+d[c]*oa*(255-a)/255)/na;d[3]=na;}
  decoded[idx]=true;
 }
 for(bool d:decoded)if(!d)return false;return true;
}
static Atlas pixels;static std::atomic<int> state{0};static uint64_t texture;
}
bool laonline_map_art(LaoMapArt *out){
 int s=state.load(std::memory_order_acquire);
 if(!s){state=1;std::string path=std::string(g_romfs_dir?g_romfs_dir:"extracted/romfs")+"/region_common/ui/Game.arc";try{std::thread([path](){bool ok=false;try{ok=decode(path.c_str(),pixels);}catch(...){ok=false;}if(!ok)pixels.rgba.clear();state.store(ok?2:3,std::memory_order_release);}).detach();}catch(...){state=3;}return false;}
 if(s!=2)return false;
 if(!texture){
#ifdef __APPLE__
  id<MTLDevice> dev=(__bridge id<MTLDevice>)ui_metal_device();if(dev){MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:pixels.w height:pixels.h mipmapped:NO];id<MTLTexture> t=[dev newTextureWithDescriptor:d];[t replaceRegion:MTLRegionMake2D(0,0,pixels.w,pixels.h) mipmapLevel:0 withBytes:pixels.rgba.data() bytesPerRow:pixels.w*4];texture=(uint64_t)(__bridge_retained void *)t;}
#else
  texture=vk_imgui_texture_rgba(pixels.rgba.data(),pixels.w,pixels.h);
#endif
  if(texture){pixels.rgba.clear();pixels.rgba.shrink_to_fit();}
 }
 if(!texture)return false;*out={texture,pixels.left,pixels.top,pixels.right,pixels.bottom};return true;
}


#include "../hle/laonline_dungeon_map.h"
#include <map>
#include <memory>
namespace {
// Bounded reader for native .leb FixedHash grid entries. Matches upstream
// RandomizerCore.Tools.fixed_hash and rooms.load_room; ignores all actor data.
static Bytes fixed_member(Bytes b,size_t offset,const char *name,bool nested,size_t *nested_offset=nullptr){
 if(!b.has(offset,8))return {};
 size_t buckets=b.u16(offset+2);if(buckets>4096)return {};
 size_t entries=((offset+8+4*(buckets+1)+3)&~size_t(7))+8;
 if(!b.has(entries-8,8))return {};size_t length=b.u64(entries-8);if(length%16 || length>65536 || !b.has(entries,length))return {};
 size_t count=length/16,eo=entries+length+8;
 size_t data=((eo+4*count+7)&~size_t(7))+8;
 if(!b.has(data-8,8))return {};size_t dz=b.u64(data-8);if(!b.has(data,dz))return {};
 size_t names=((data+dz+3)&~size_t(3))+4;if(!b.has(names-4,4))return {};
 size_t nz=b.u32(names-4);if(!b.has(names,nz))return {};
 for(size_t i=0;i<count;i++){size_t e=entries+16*i,n=b.u16(e+2),p=data+b.u32(e+12),z=strlen(name);
  if(n>=nz || z>=nz-n || !b.eq(names+n,name,z) || b.p[names+n+z])continue;
  unsigned node=b.u16(e);if(nested){if(node>0xffed || !b.has(p,8))return {};if(nested_offset)*nested_offset=p;return b;}
  if(node<0xfff0 || !b.has(p,8))return {};size_t len=b.u64(p);if(!b.has(p+8,len))return {};return {b.p+p+8,len};
 }return {};
}
static bool room_grid(const std::string &path,int row,int col,Bytes &grid,std::vector<uint8_t> &file,float *ox=nullptr,float *oz=nullptr){
 FILE *f=fopen(path.c_str(),"rb");if(!f)return false;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 if(n<32 || n>4*1024*1024){fclose(f);return false;}file.resize(n);bool ok=fread(file.data(),1,n,f)==size_t(n);fclose(f);if(!ok)return false;
 size_t goff=0;Bytes all{file.data(),file.size()},g=fixed_member(all,0,"grid",true,&goff),info=fixed_member(g,goff,"info",false);grid=fixed_member(g,goff,"data",false);
 if(!info.has(0,16) || info.u16(0)!=8 || info.u16(2)!=10 || !grid.has(0,80*16))return false;
 float x=info.f32(8),z=info.f32(12);if(!std::isfinite(x)||!std::isfinite(z)||fabsf(x)>10000||fabsf(z)>10000)return false;if(ox)*ox=x;if(oz)*oz=z;return true;
}
using RoomOffsets=std::map<std::string,std::pair<float,float>>;
struct DungeonArt {Atlas pixels;RoomOffsets offsets;std::atomic<int> state{0};uint64_t texture=0;};
static std::map<std::string,std::unique_ptr<DungeonArt>> dungeon_art;
static bool decode_dungeon(const std::string &root,const std::string &level,float sx,float sy,Atlas &a,RoomOffsets *offsets=nullptr){
 a.w=640;a.h=512;a.left=0;a.top=0;a.right=120;a.bottom=96;a.rgba.assign(a.w*a.h*4,0);unsigned rooms=0;
 for(const auto &cell:lao_dungeon_cells){if(level!=cell.level || fabsf(cell.sx-sx)>.01f || fabsf(cell.sy-sy)>.01f || cell.row<1 || cell.row>8 || cell.col<0 || cell.col>7)continue;
  float ox=0,oz=0;std::vector<uint8_t> file;Bytes grid;if(!room_grid(root+"/region_common/level/"+level+"/"+cell.room+".leb",cell.row,cell.col,grid,file,&ox,&oz))continue;
  if(offsets)(*offsets)[cell.room]={cell.col*15.f-ox,(cell.row-1)*12.f-oz};
  rooms++;
  for(int y=0;y<8;y++)for(int x=0;x<10;x++){size_t t=16*(10*y+x);bool water=(grid.p[t+2]&4)&&grid.f32(t+12)<-.3f;bool wall=(grid.p[t]&2)&&!(grid.p[t+2]&8);
   uint8_t r=wall?40:water?50:166,g=wall?49:water?91:176,b=wall?38:water?111:100;
   for(int py=0;py<8;py++)for(int px=0;px<8;px++){int ax=(cell.col*10+x)*8+px,ay=((cell.row-1)*8+y)*8+py;size_t d=4*(ay*a.w+ax);a.rgba[d]=r;a.rgba[d+1]=g;a.rgba[d+2]=b;a.rgba[d+3]=255;}
  }
 }return rooms>0;
}
}
bool laonline_dungeon_art(const char *level,float sx,float sy,LaoMapArt *out){
 if(!level || !out)return false;char floor[64];snprintf(floor,sizeof floor,"%s:%.1f:%.1f",level,sx,sy);
 auto &entry=dungeon_art[floor];if(!entry){if(dungeon_art.size()>24){dungeon_art.erase(floor);return false;}entry=std::make_unique<DungeonArt>();}
 DungeonArt *d=entry.get();int state=d->state.load(std::memory_order_acquire);
 if(!state){d->state=1;std::string root=g_romfs_dir?g_romfs_dir:"extracted/romfs",lv=level;try{std::thread([d,root,lv,sx,sy](){bool ok=false;try{ok=decode_dungeon(root,lv,sx,sy,d->pixels,&d->offsets);}catch(...){ok=false;}d->state.store(ok?2:3,std::memory_order_release);}).detach();}catch(...){d->state=3;}return false;}
 if(state!=2)return false;
 if(!d->texture){
#ifdef __APPLE__
  id<MTLDevice> dev=(__bridge id<MTLDevice>)ui_metal_device();if(dev){MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:d->pixels.w height:d->pixels.h mipmapped:NO];id<MTLTexture> t=[dev newTextureWithDescriptor:desc];[t replaceRegion:MTLRegionMake2D(0,0,d->pixels.w,d->pixels.h) mipmapLevel:0 withBytes:d->pixels.rgba.data() bytesPerRow:d->pixels.w*4];d->texture=(uint64_t)(__bridge_retained void *)t;}
#else
  d->texture=vk_imgui_texture_rgba(d->pixels.rgba.data(),d->pixels.w,d->pixels.h);
#endif
  if(d->texture){d->pixels.rgba.clear();d->pixels.rgba.shrink_to_fit();}
 }
 if(!d->texture)return false;*out={d->texture,d->pixels.left,d->pixels.top,d->pixels.right,d->pixels.bottom};return true;
}

bool laonline_dungeon_position(const char *level,float sx,float sy,const char *room,float x,float z,float *mx,float *mz){
 if(!level||!room||!mx||!mz||!std::isfinite(x)||!std::isfinite(z))return false;
 char key[64];snprintf(key,sizeof key,"%s:%.1f:%.1f",level,sx,sy);auto i=dungeon_art.find(key);
 if(i==dungeon_art.end()||i->second->state.load(std::memory_order_acquire)!=2)return false;
 auto o=i->second->offsets.find(room);if(o==i->second->offsets.end())return false;
 *mx=x+o->second.first;*mz=z+o->second.second;return true;
}
