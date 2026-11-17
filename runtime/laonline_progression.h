#pragma once
// Semantic progression engine. Transport adapters must supply authenticated authority,
// never an identity copied from a packet. No native addresses or game memory here.
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <mutex>
#include <functional>
namespace lao { namespace progression {
constexpr uint16_t protocol_version=1;
enum class Type:uint8_t {QUEST_STAGE_ADVANCE=1,DUNGEON_DOOR_UNLOCK=2,KEY_ITEM_OBTAINED=3,BOSS_DEFEATED=4};
enum class Result {Accepted,Duplicate,Gap,Unauthorized,Incompatible,Invalid,IllegalTransition,Conflict,Capacity,StorageFailure};
struct Context {std::string campaign,content,revision;bool operator==(const Context &b)const{return campaign==b.campaign&&content==b.content&&revision==b.revision;}};
struct Authority {uint64_t peer;bool authenticated;};
struct GameEvent {uint64_t sequence=0;Type type=Type::QUEST_STAGE_ADVANCE;uint64_t target=0;uint32_t from=0,value=0;bool operator==(const GameEvent &b)const{return sequence==b.sequence&&type==b.type&&target==b.target&&from==b.from&&value==b.value;}};
struct Prerequisite {Type kind;uint64_t target;uint32_t stage=0;};
struct Stage {uint32_t id;std::map<size_t,bool> flags;};
struct Quest {uint32_t initial;std::map<uint32_t,Stage> stages;std::set<std::pair<uint32_t,uint32_t>> transitions;std::map<uint32_t,std::vector<Prerequisite>> prerequisites;};
struct Permanent {std::map<size_t,bool> flags;std::vector<Prerequisite> prerequisites;};
struct Registry {std::string revision,evidence;size_t flag_count=0;std::map<uint64_t,Quest> quests;std::map<uint64_t,Permanent> doors,items,bosses;};
struct State {uint64_t sequence=0;std::map<uint64_t,uint32_t> quests;std::set<uint64_t> doors,items,bosses;bool operator==(const State &b)const{return sequence==b.sequence&&quests==b.quests&&doors==b.doors&&items==b.items&&bosses==b.bosses;}};
// Controlled flag image, NOT a declaration of the original game's save layout.
// Platform adapters must build this from verified APIs at a safe game-thread point.
struct LegacySaveBuffer {Context context;bool owned=false,safe_to_write=false;std::vector<uint8_t> flags;uint32_t rupees=0,health=0;std::vector<uint32_t> consumables;};
struct Checkpoint {Context context;State state;uint64_t digest=0;};
struct Catchup {Context context;Result result=Result::Invalid;uint64_t through=0,digest=0;bool has_checkpoint=false;Checkpoint checkpoint;std::vector<GameEvent> events;};
struct Commit {Result result;uint64_t expected_sequence;};
using DurableCommit=std::function<bool(const Context &,const std::vector<GameEvent>&)>;
class SyncServer {
 public:
 SyncServer(Context context,uint64_t host,Registry registry,DurableCommit persist);
 Commit receive_host_event(const Authority &,const Context &,const GameEvent &);
 Catchup generate_catchup_stream_for_player(const Context &,uint64_t last_known_sequence_id)const;
 Result apply_state_to_legacy_buffer(LegacySaveBuffer &)const;
 Checkpoint checkpoint()const;
 Result restore(const Context &,const std::vector<GameEvent>&);
 Result reconcile(const Authority &,const Catchup &); // replica-only state replacement, never saves personal resources
 State state()const;
 static uint64_t digest(const State &);
 private:
 Result transition(State &,const GameEvent &)const;
 bool prerequisites(const State &,const std::vector<Prerequisite>&)const;
 bool validate_state(const State &)const;
 std::map<size_t,bool> projection(const State &)const;
 mutable std::mutex mutex_;Context context_;uint64_t host_;Registry registry_;DurableCommit persist_;
 bool replica_=false;State initial_,state_;std::vector<GameEvent> journal_;
 static constexpr size_t max_events=65536,max_catchup=256;
};
// Atomic journal codec: fixed little-endian values, no pointers/native offsets.
std::vector<uint8_t> encode_journal(const Context &,const std::vector<GameEvent>&);
bool decode_journal(const std::vector<uint8_t>&,Context &,std::vector<GameEvent>&);
bool write_journal_atomic(const std::string &path,const Context &,const std::vector<GameEvent>&);
}}
