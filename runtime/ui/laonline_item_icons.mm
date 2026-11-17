// User-supplied artwork. Never extracts or changes game/model textures.
#include "laonline_item_icons.h"
#include "../paths.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <map>
#ifdef __APPLE__
#import <Metal/Metal.h>
extern "C" void *ui_metal_device(void);
#else
extern "C" uint64_t vk_imgui_texture_rgba(const void *,int,int);
extern "C" uint64_t vk_imgui_texture_generation(void);
#endif
static const char *filename(int item){switch(item){
 case 0:return "icon-760b1fc4c4d9.png";
 case 1:return "icon-51def4268111.png";
 case 2:return "icon-deee0e6504bc.png";
 case 3:return "icon-43a00925e853.png";
 case 4:return "icon-1f24eb92a3f4.png";
 case 5:return "icon-c0d4cd4b7d47.png";
 case 6:return "icon-cec61c1dbb60.png";
 case 7:return "icon-6e714bf006c5.png";
 case 8:return "icon-256edd9d494b.png";
 case 9:return "icon-d70dfb17ace8.png";
 case 10:return "icon-1ad2c4dcc17b.png";
 case 11:return "icon-ab9903c6e933.png";
 case 12:return "icon-dea14a890715.png";
 case 13:return "icon-7028101ee0a5.png";
 case 14:return "icon-7028101ee0a5.png";
 case 15:return "icon-5b2064850701.png";
 case 16:return "icon-5982be1d8608.png";
 case 17:return "icon-d771570dda66.png";
 case 18:return "icon-9cf4582623c4.png";
 case 19:return "icon-4b3e26c90576.png";
 case 20:return "icon-f8fcadba2d69.png";
 case 21:return "icon-0d6c60f79ac3.png";
 case 22:return "icon-a7b3788465d5.png";
 case 23:return "icon-4408b861a709.png";
 case 24:return "icon-c44b74d2a515.png";
 case 25:return "icon-cf254db70926.png";
 case 26:return "icon-4cf8606178ce.png";
 case 27:return "icon-7d04912f2d8d.png";
 case 28:return "icon-a17f87dcd36f.png";
 case 30:return "icon-e5c6bdfadf67.png";
 case 31:return "icon-567bbc455364.png";
 case 32:return "icon-185ea5339eba.png";
 case 33:return "icon-d8830aab9903.png";
 case 34:return "icon-8e1318a6d3f4.png";
 case 35:return "icon-500e0d91eb40.png";
 case 36:return "icon-dfdde089214c.png";
 case 37:return "icon-d6fed7faaf5b.png";
 case 38:return "icon-169c23378d90.png";
 case 39:return "icon-12b39186a290.png";
 case 42:return "icon-0ec43c4684d7.png";
 case 43:return "icon-2f0d24f3987b.png";
 case 44:return "icon-31e935654f42.png";
 case 45:return "icon-84396a37bd10.png";
 case 47:return "icon-10eba1666ec4.png";
 case 48:return "icon-ca283284d825.png";
 case 49:return "icon-4b68d8622f39.png";
 case 50:return "icon-8a7017187fb4.png";
 case 51:return "icon-498dc66c1148.png";
 case 52:return "icon-20cd5d15d82e.png";
 case 53:return "icon-96e7753570aa.png";
 case 54:return "icon-7c859fd6c4a5.png";
 case 55:return "icon-406c07b60115.png";
 case 56:return "icon-c5490ce24a42.png";
 case 57:return "icon-4265b59c9395.png";
 case 58:return "icon-efc669c67ba3.png";
 case 59:return "icon-611c9f5a6300.png";
 case 60:return "icon-850dfe8097a7.png";
 case 61:return "icon-f3a022bce879.png";
 case 62:return "icon-783b247f65e9.png";
 case 100:return "icon-c2ee557cb15d.png";
 case 101:return "icon-f5e4f5b80001.png";
 case 102:return "icon-504f75aa19ed.png";
 case 104:return "icon-446e522a2a15.png";
 case 105:return "icon-91ca3c8ec57f.png";
 case 106:return "icon-aeddbf6c4301.png";
 case 108:return "icon-f24ba858104c.png";
 case 110:return "icon-b1f5ac31a50a.png";
 case 111:return "icon-6c8c73dc2b0b.png";
 case 112:return "icon-74951f77cebb.png";
 case 113:return "icon-837c39900f4f.png";
 case 114:return "icon-5e37cc4cdd12.png";
 case 115:return "icon-0316a600e789.png";
 case 116:return "icon-a35cc8a9141b.png";
 case 126:return "icon-b1f5ac31a50a.png";
 default:return nullptr;}}
struct CachedIcon{ImTextureID texture;float aspect;bool tried;};
struct IconContext{uint64_t generation=0;CachedIcon items[127]={};CachedIcon avatars[12][13]={};};
static std::map<ImGuiContext*,IconContext> icon_contexts;
static IconContext &icon_context(){auto &cache=icon_contexts[ImGui::GetCurrentContext()];
#ifndef __APPLE__
 uint64_t generation=vk_imgui_texture_generation();if(cache.generation!=generation){cache=IconContext();cache.generation=generation;}
#endif
 return cache;}

bool laonline_item_icon(int item,ImTextureID *texture,float *aspect){
 if(item<0||item>=127||!texture||!aspect)return false;
 const char *name=filename(item);if(!name)return false;
 CachedIcon &c=icon_context().items[item];
 if(!c.tried){
  c.tried=true;char relative[256],path[1200];snprintf(relative,sizeof relative,"laonline-items/%s",name);
  if(!rt_resource(relative,nullptr,path,sizeof path))return false;
  SDL_Surface *s=SDL_LoadPNG(path);if(!s)return false;
  if(s->w<1||s->h<1||s->w>2048||s->h>2048){SDL_DestroySurface(s);return false;}
  SDL_Surface *rgba=SDL_ConvertSurface(s,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(s);if(!rgba)return false;
  // SDL surfaces can have pitch padding. Upload only contiguous rows.
  const size_t bytes=(size_t)rgba->w*rgba->h*4;
  uint8_t *pixels=(uint8_t *)SDL_malloc(bytes);if(!pixels){SDL_DestroySurface(rgba);return false;}
  for(int y=0;y<rgba->h;y++)memcpy(pixels+(size_t)y*rgba->w*4,(uint8_t *)rgba->pixels+(size_t)y*rgba->pitch,(size_t)rgba->w*4);
#ifdef __APPLE__
  id<MTLDevice> dev=(__bridge id<MTLDevice>)ui_metal_device();
  if(dev){MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:rgba->w height:rgba->h mipmapped:NO];id<MTLTexture> t=[dev newTextureWithDescriptor:d];[t replaceRegion:MTLRegionMake2D(0,0,rgba->w,rgba->h) mipmapLevel:0 withBytes:pixels bytesPerRow:rgba->w*4];c.texture=(ImTextureID)(__bridge_retained void *)t;}
#else
  c.texture=(ImTextureID)vk_imgui_texture_rgba(pixels,rgba->w,rgba->h);
#endif
  c.aspect=(float)rgba->w/rgba->h;SDL_free(pixels);SDL_DestroySurface(rgba);
 }
 *texture=c.texture;*aspect=c.aspect;return c.texture!=0;
}

// Effective named colour palette, matching Cosmetic tab order 1..12.
static int avatar_colour(const uint8_t rgb[3]){
 static const uint32_t palette[]={0xD63A3A,0xF08A24,0xE8B830,0x7A4A28,0x3FA34D,0x22A7A0,0x2F6FD6,0x8A4FD8,0xE86AA8,0xF2F0EA,0x2A2A2E,0xA9B0B8};
 int best=0,distance=0x7fffffff;for(int i=0;i<12;i++){int r=int(palette[i]>>16)-rgb[0],g=int((palette[i]>>8)&255)-rgb[1],b=int(palette[i]&255)-rgb[2],d=r*r+g*g+b*b;if(d<distance){distance=d;best=i;}}return best;
}
bool laonline_avatar_icon(const uint8_t cap[3],const uint8_t hair[3],ImTextureID *texture,float *aspect){
 int hat=avatar_colour(cap),h=(hair[0]==0xff&&hair[1]==0xdf&&hair[2]==0x57)?0:avatar_colour(hair)+1;CachedIcon &c=icon_context().avatars[hat][h];
 if(!c.tried){c.tried=true;char relative[128],path[1200];snprintf(relative,sizeof relative,"laonline-avatars/cap-%02d-hair-%02d.png",hat+1,h);
  if(!rt_resource(relative,nullptr,path,sizeof path))return false;SDL_Surface *s=SDL_LoadPNG(path);if(!s)return false;
  if(s->w<1||s->h<1||s->w>512||s->h>512){SDL_DestroySurface(s);return false;}
  SDL_Surface *rgba=SDL_ConvertSurface(s,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(s);if(!rgba)return false;
  uint8_t pixels[256*256*4];if(rgba->w!=256||rgba->h!=256){SDL_DestroySurface(rgba);return false;}
  for(int y=0;y<256;y++)memcpy(pixels+y*1024,(uint8_t*)rgba->pixels+y*rgba->pitch,1024);
#ifdef __APPLE__
  id<MTLDevice> dev=(__bridge id<MTLDevice>)ui_metal_device();if(dev){MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:256 height:256 mipmapped:NO];id<MTLTexture> t=[dev newTextureWithDescriptor:d];[t replaceRegion:MTLRegionMake2D(0,0,256,256) mipmapLevel:0 withBytes:pixels bytesPerRow:1024];c.texture=(ImTextureID)(__bridge_retained void*)t;}
#else
  c.texture=(ImTextureID)vk_imgui_texture_rgba(pixels,256,256);
#endif
  c.aspect=1;SDL_DestroySurface(rgba);
 }
 *texture=c.texture;*aspect=c.aspect;return c.texture!=0;
}
