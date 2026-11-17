#include "laonline_progression.h"
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
namespace lao { namespace progression {
static bool context_valid(const Context &c){return !c.campaign.empty()&&c.campaign.size()<=64&&!c.content.empty()&&c.content.size()<=64&&!c.revision.empty()&&c.revision.size()<=32;}
SyncServer::SyncServer(Context c,uint64_t host,Registry r,DurableCommit persist):context_(std::move(c)),host_(host),registry_(std::move(r)),persist_(std::move(persist)){
 if(!context_valid(context_)||!host_||registry_.revision!=context_.revision||registry_.evidence.empty()||!registry_.flag_count||registry_.flag_count>65536)throw std::invalid_argument("unverified progression registry/context");
 std::map<size_t,std::pair<Type,uint64_t>> owners;
 auto fields=[&](const std::map<size_t,bool>& f,Type t,uint64_t id){for(auto x:f){if(x.first>=registry_.flag_count)throw std::invalid_argument("projection bounds");auto owner=std::make_pair(t,id);auto at=owners.find(x.first);if(at!=owners.end()&&at->second!=owner)throw std::invalid_argument("overlapping semantic ownership");owners[x.first]=owner;}};
 for(const auto &q:registry_.quests){
  if(!q.first||!q.second.stages.count(q.second.initial))throw std::invalid_argument("quest initial stage");
  std::set<size_t> bits;
  for(const auto &s:q.second.stages){if(s.first!=s.second.id)throw std::invalid_argument("stage identity");fields(s.second.flags,Type::QUEST_STAGE_ADVANCE,q.first);for(auto f:s.second.flags)bits.insert(f.first);}
  // Every stage must specify every owned flag, including explicit clears.
  for(const auto &s:q.second.stages){if(s.second.flags.size()!=bits.size())throw std::invalid_argument("incomplete exclusive-stage projection");}
  for(auto t:q.second.transitions)if(t.first==t.second||!q.second.stages.count(t.first)||!q.second.stages.count(t.second))throw std::invalid_argument("invalid transition registry");
  initial_.quests[q.first]=q.second.initial;
 }
 auto permanent=[&](const auto &defs,Type t){for(const auto &d:defs){if(!d.first)throw std::invalid_argument("zero target");fields(d.second.flags,t,d.first);}};
 permanent(registry_.doors,Type::DUNGEON_DOOR_UNLOCK);permanent(registry_.items,Type::KEY_ITEM_OBTAINED);permanent(registry_.bosses,Type::BOSS_DEFEATED);
 auto predicates_valid=[&](const std::vector<Prerequisite>& req){for(auto p:req){bool ok=false;switch(p.kind){case Type::QUEST_STAGE_ADVANCE:{auto q=registry_.quests.find(p.target);ok=q!=registry_.quests.end()&&q->second.stages.count(p.stage);break;}case Type::KEY_ITEM_OBTAINED:ok=registry_.items.count(p.target)&&!p.stage;break;case Type::DUNGEON_DOOR_UNLOCK:ok=registry_.doors.count(p.target)&&!p.stage;break;case Type::BOSS_DEFEATED:ok=registry_.bosses.count(p.target)&&!p.stage;break;}if(!ok)throw std::invalid_argument("unknown prerequisite");}};
 for(auto &q:registry_.quests)for(auto &r:q.second.prerequisites){if(!q.second.stages.count(r.first))throw std::invalid_argument("prerequisite stage");predicates_valid(r.second);}
 for(auto *defs:{&registry_.doors,&registry_.items,&registry_.bosses})for(auto &d:*defs)predicates_valid(d.second.prerequisites);
 state_=initial_;
}
bool SyncServer::prerequisites(const State &s,const std::vector<Prerequisite>& req)const{
 for(auto p:req){bool ok=false;switch(p.kind){case Type::QUEST_STAGE_ADVANCE:{auto q=s.quests.find(p.target);ok=q!=s.quests.end()&&q->second==p.stage;break;}case Type::KEY_ITEM_OBTAINED:ok=s.items.count(p.target);break;case Type::DUNGEON_DOOR_UNLOCK:ok=s.doors.count(p.target);break;case Type::BOSS_DEFEATED:ok=s.bosses.count(p.target);break;}if(!ok)return false;}return true;
}
Result SyncServer::transition(State &s,const GameEvent &e)const{
 if(e.sequence!=s.sequence+1||!e.target)return Result::Invalid;
 if(e.type==Type::QUEST_STAGE_ADVANCE){
  auto q=registry_.quests.find(e.target);if(q==registry_.quests.end()||!q->second.stages.count(e.value))return Result::Invalid;
  auto current=s.quests.find(e.target);if(current==s.quests.end()||current->second!=e.from||!q->second.transitions.count({e.from,e.value}))return Result::IllegalTransition;
  auto req=q->second.prerequisites.find(e.value);if(req!=q->second.prerequisites.end()&&!prerequisites(s,req->second))return Result::IllegalTransition;
  current->second=e.value;
 }else{
  if(e.from||e.value!=1)return Result::Invalid;
  const std::map<uint64_t,Permanent> *defs=nullptr;std::set<uint64_t> *dest=nullptr;
  switch(e.type){case Type::DUNGEON_DOOR_UNLOCK:defs=&registry_.doors;dest=&s.doors;break;case Type::KEY_ITEM_OBTAINED:defs=&registry_.items;dest=&s.items;break;case Type::BOSS_DEFEATED:defs=&registry_.bosses;dest=&s.bosses;break;default:return Result::Invalid;}
  auto d=defs->find(e.target);if(d==defs->end())return Result::Invalid;
  if(dest->count(e.target))return Result::Duplicate;
  if(!prerequisites(s,d->second.prerequisites))return Result::IllegalTransition;
  dest->insert(e.target);
 }
 s.sequence=e.sequence;return Result::Accepted;
}
Commit SyncServer::receive_host_event(const Authority &auth,const Context &context,const GameEvent &e){
 std::lock_guard<std::mutex> guard(mutex_);auto done=[&](Result r){return Commit{r,state_.sequence+1};};
 if(replica_||!auth.authenticated||auth.peer!=host_)return done(Result::Unauthorized);
 if(!(context==context_))return done(Result::Incompatible);
 if(!e.sequence||state_.sequence==std::numeric_limits<uint64_t>::max())return done(Result::Invalid);
 if(e.sequence<=state_.sequence){auto at=(size_t)(e.sequence-1);return done(at<journal_.size()&&journal_[at]==e?Result::Duplicate:Result::Conflict);}
 if(e.sequence!=state_.sequence+1)return done(Result::Gap);
 if(journal_.size()>=max_events)return done(Result::Capacity);
 State candidate=state_;Result result=transition(candidate,e);if(result!=Result::Accepted)return done(result);
 auto next=journal_;next.push_back(e);
 // Called by a progression worker, never a render/network callback that writes native state.
 // Persistence must atomically replace the entire encoded journal or return false.
 try{if(!persist_||!persist_(context_,next))return done(Result::StorageFailure);}catch(...){return done(Result::StorageFailure);}
 state_=std::move(candidate);journal_=std::move(next);return done(Result::Accepted);
}
uint64_t SyncServer::digest(const State &s){
 uint64_t h=1469598103934665603ull;auto add=[&](uint64_t n){for(int i=0;i<8;i++){h^=(n>>(i*8))&255;h*=1099511628211ull;}};
 add(s.sequence);add(s.quests.size());for(auto q:s.quests){add(q.first);add(q.second);}
 for(const auto *set:{&s.doors,&s.items,&s.bosses}){add(set->size());for(auto x:*set)add(x);}return h; // divergence checksum, not authentication
}
State SyncServer::state()const{std::lock_guard<std::mutex> g(mutex_);return state_;}
Checkpoint SyncServer::checkpoint()const{std::lock_guard<std::mutex> g(mutex_);return {context_,state_,digest(state_)};}
Catchup SyncServer::generate_catchup_stream_for_player(const Context &context,uint64_t ack)const{
 std::lock_guard<std::mutex> g(mutex_);Catchup out;out.context=context_;
 if(replica_){out.result=Result::Invalid;return out;}
 if(!(context==context_)){out.result=Result::Incompatible;return out;}
 if(ack>state_.sequence){out.result=Result::Conflict;return out;}
 out.result=Result::Accepted;out.through=state_.sequence;out.digest=digest(state_);
 if(state_.sequence-ack>max_catchup){out.has_checkpoint=true;out.checkpoint={context_,state_,out.digest};}
 else out.events.assign(journal_.begin()+(size_t)ack,journal_.end());
 // History is retained for recovery/deduplication. No compaction of side effects.
 return out;
}
bool SyncServer::validate_state(const State &s)const{
 if(s.quests.size()!=registry_.quests.size()||s.sequence>max_events)return false;
 for(auto q:s.quests){auto d=registry_.quests.find(q.first);if(d==registry_.quests.end()||!d->second.stages.count(q.second))return false;}
 for(auto x:s.doors)if(!registry_.doors.count(x))return false;
 for(auto x:s.items)if(!registry_.items.count(x))return false;
 for(auto x:s.bosses)if(!registry_.bosses.count(x))return false;return true;
}
Result SyncServer::restore(const Context &context,const std::vector<GameEvent>& events){
 std::lock_guard<std::mutex> g(mutex_);if(!(context==context_))return Result::Incompatible;
 if(state_.sequence||!journal_.empty())return Result::Conflict;
 if(events.size()>max_events)return Result::Capacity;
 State candidate=initial_;for(auto e:events){Result r=transition(candidate,e);if(r!=Result::Accepted)return r;}
 state_=std::move(candidate);journal_=events;return Result::Accepted;
}
Result SyncServer::reconcile(const Authority &auth,const Catchup &catchup){
 std::lock_guard<std::mutex> g(mutex_);if(!auth.authenticated||auth.peer!=host_)return Result::Unauthorized;if(!(catchup.context==context_))return Result::Incompatible;
 if(catchup.result!=Result::Accepted)return catchup.result;
 State candidate=state_;
 if(catchup.has_checkpoint){if(!(catchup.checkpoint.context==context_))return Result::Incompatible;if(catchup.checkpoint.state.sequence<candidate.sequence)return Result::Conflict;candidate=catchup.checkpoint.state;if(!validate_state(candidate)||digest(candidate)!=catchup.checkpoint.digest)return Result::Invalid;}
 for(auto e:catchup.events){if(e.sequence<=candidate.sequence)return Result::Conflict;Result r=transition(candidate,e);if(r!=Result::Accepted)return r;}
 if(candidate.sequence!=catchup.through||digest(candidate)!=catchup.digest)return Result::Gap;
 replica_=true;state_=std::move(candidate);journal_.clear(); // replica is not permitted to become a host from a compact snapshot
 return Result::Accepted;
}
std::map<size_t,bool> SyncServer::projection(const State &s)const{
 std::map<size_t,bool> out;for(auto q:s.quests)for(auto f:registry_.quests.at(q.first).stages.at(q.second).flags)out[f.first]=f.second;
 // Permanent discoveries only set mapped unlocks; they never spend keys or grant quantities.
 for(auto pair:{std::make_pair(&s.doors,&registry_.doors),std::make_pair(&s.items,&registry_.items),std::make_pair(&s.bosses,&registry_.bosses)})for(auto id:*pair.first)for(auto f:pair.second->at(id).flags)out[f.first]=f.second;
 return out;
}
Result SyncServer::apply_state_to_legacy_buffer(LegacySaveBuffer &target)const{
 std::lock_guard<std::mutex> g(mutex_);
 if(!(target.context==context_))return Result::Incompatible;
 if(!target.owned||!target.safe_to_write||target.flags.size()!=registry_.flag_count)return Result::Invalid;
 auto candidate=target.flags;for(auto f:projection(state_))candidate[f.first]=(uint8_t)f.second;
 target.flags.swap(candidate);return Result::Accepted; // rupees, health, consumables untouched
}
static uint64_t checksum(const uint8_t *p,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ull;}return h;}
static void put(std::vector<uint8_t>& b,uint64_t n,int bytes){for(int i=0;i<bytes;i++)b.push_back((uint8_t)(n>>(8*i)));}
std::vector<uint8_t> encode_journal(const Context &c,const std::vector<GameEvent>& events){
 if(!context_valid(c)||events.size()>65536)throw std::invalid_argument("journal bounds");
 std::vector<uint8_t>b={'L','A','O','P'};put(b,protocol_version,2);
 for(const auto *text:{&c.campaign,&c.content,&c.revision}){put(b,text->size(),1);b.insert(b.end(),text->begin(),text->end());}
 put(b,events.size(),4);for(auto e:events){put(b,e.sequence,8);put(b,(uint8_t)e.type,1);put(b,e.target,8);put(b,e.from,4);put(b,e.value,4);}put(b,checksum(b.data(),b.size()),8);return b;
}
bool decode_journal(const std::vector<uint8_t>& bytes,Context &context,std::vector<GameEvent>& events){
 if(bytes.size()>2*1024*1024||bytes.size()<21)return false;
 uint64_t stored=0;for(int i=0;i<8;i++)stored|=(uint64_t)bytes[bytes.size()-8+i]<<(8*i);if(stored!=checksum(bytes.data(),bytes.size()-8))return false;
 size_t at=0;auto get=[&](int n){if(at+(size_t)n>bytes.size())throw std::out_of_range("journal");uint64_t v=0;for(int i=0;i<n;i++)v|=(uint64_t)bytes[at++]<<(8*i);return v;};
 try{if(get(4)!=0x504f414c||get(2)!=protocol_version)return false;Context c;for(auto text:{&c.campaign,&c.content,&c.revision}){size_t n=get(1);if(n>64||at+n>bytes.size())return false;text->assign((const char *)bytes.data()+at,n);at+=n;}if(!context_valid(c))return false;
  size_t n=get(4);if(n>65536||bytes.size()-at!=n*25+8)return false;std::vector<GameEvent> out;out.reserve(n);for(size_t i=0;i<n;i++){GameEvent e;e.sequence=get(8);e.type=(Type)get(1);e.target=get(8);e.from=(uint32_t)get(4);e.value=(uint32_t)get(4);if(e.sequence!=i+1||(uint8_t)e.type<1||(uint8_t)e.type>4)return false;out.push_back(e);}context=std::move(c);events=std::move(out);return true;
 }catch(...){return false;}
}
bool write_journal_atomic(const std::string &path,const Context &context,const std::vector<GameEvent>& events){
 // One journal writer per campaign. The caller owns this fixed server-side path.
 std::vector<uint8_t> bytes;try{bytes=encode_journal(context,events);}catch(...){return false;}
 std::string temporary=path+".pending";FILE *f=fopen(temporary.c_str(),"wb");if(!f)return false;
 bool ok=fwrite(bytes.data(),1,bytes.size(),f)==bytes.size()&&fflush(f)==0;
#ifdef _WIN32
 if(ok)ok=_commit(_fileno(f))==0;
#else
 if(ok)ok=fsync(fileno(f))==0;
#endif
 if(fclose(f)!=0)ok=false;if(!ok){remove(temporary.c_str());return false;}
#ifdef _WIN32
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0),m=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,temporary.c_str(),-1,nullptr,0);if(n<1||m<1){remove(temporary.c_str());return false;}
 std::wstring destination(n,L'\0'),source(m,L'\0');MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,&destination[0],n);MultiByteToWideChar(CP_UTF8,0,temporary.c_str(),-1,&source[0],m);
 ok=MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 ok=rename(temporary.c_str(),path.c_str())==0;
#endif
 if(!ok)remove(temporary.c_str());return ok;
}
}}
