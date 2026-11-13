// Keysanity: dungeon items found anywhere. The game's AddItem (main+0x7e18b0, Items.gsheet itemID ids) adds small
// keys / Nightmare Key / map / compass / beak to the CURRENT dungeon only (outside one it does nothing). Seeds with
// keysanity give them with a dungeon index (upstream LAS dev convention: 0-7 = Tail Cave..Turtle Rock, 9 = Color
// Dungeon): this hook
// (tools/patches.py ADD_ITEM_SITE: the inner add main+0x8d08e0 that AddItem, chests and drops all call) applies those straight to that dungeon's save record.
// index -1 (vanilla behaviour, the item's own dungeon) is left to the game.
//
// Save record (notes/save_format.md): live save + 0xE0 + 0x10 * rec, dungeon n -> rec 2(n-1), Color Dungeon -> 18;
// +0 small keys held, +1 bits: compass 0, map 1, beak 2, Nightmare Key 3.
#include "../game_addresses.h"
#include <stdio.h>
#include <string.h>

#include "../hle.h"
#include "../rando.h"


// item ids as AddItem gets them: Items.gsheet `itemID` (not the row number; rows 41+ differ)
enum { IT_COMPASS = 44, IT_MAP = 45, IT_BEAK = 47, IT_SMALL_KEY = 48, IT_NIGHTMARE_KEY = 49 };

// Pot filler refills (tools/rando/pots.py REFILL_INDEX): bombs / magic powder / arrows only once Link has the bomb
// bag / powder / bow (a refill would otherwise unlock them - "bombs must be found first"); before that a blue rupee.
// Inventory bits: live save +0x5568 (notes/save_format.md): bit 2 bombs, bit 3 bow, bit 8 powder.
enum { IT_BOMB = 4, IT_POWDER = 11, IT_ARROW = 60, IT_RUPEE_BLUE = 102, REFILL_INDEX = 80 };

/* V12.5 (upstream-confirmed bug, beta_5.4): with "Needs Bomb Bag" / "Needs Magic Powder Bag" OFF (seed settings
 * shuffle-bombs / shuffle-powder false) bombs and powder are meant to be usable from the first one found or bought.
 * The refill rule below still waited for the item to be owned, so every bomb / powder pickup and shop purchase turned
 * into a blue rupee and the item never unlocked (seed 51SNA-9R0R4-30002-ECBAZ-SF000-0003H, report LAO-WKRLQ).
 * When the bag is NOT required, the first bomb / powder add now unlocks the item (the save editor's own bit, plus
 * GetMagicPowder for powder, which LAS checks before letting powder in) and the game's own add gives the count.
 * With the bag required, nothing changes. Arrows still need the Bow. Vanilla (no seed) is never touched. */
static int g_bag_bombs = -1, g_bag_powder = -1;  // 1 = the seed requires the bag (read once per boot)
RT_ON_RESET(bag_policy_reset) { g_bag_bombs = g_bag_powder = -1; }
bool flags_ext_get(int idx);
void flags_put(int idx, bool on);
static void bag_policy(void) {
    if (g_bag_bombs >= 0) return;
    g_bag_bombs = g_bag_powder = 1;  // no seed / unreadable settings: keep the old (strict) rule
    const char *dir = rando_seed_dir();
    static char buf[1 << 16];
    char p[512];
    FILE *f = dir ? (snprintf(p, sizeof p, "%s/settings.json", dir), fopen(p, "rb")) : NULL;
    if (!f) return;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    g_bag_bombs = strstr(buf, "\"shuffle-bombs\": false") ? 0 : 1;
    g_bag_powder = strstr(buf, "\"shuffle-powder\": false") ? 0 : 1;
    LOG("[pots] V12.5 bag rules: bombs %s, powder %s\n", g_bag_bombs ? "need the Bomb Bag" : "unlock on first pickup",
        g_bag_powder ? "need the Powder Bag" : "unlock on first pickup");
}
// Called for every add of item 4 / 11 (refill index, shops, chests, drops): unlock first when the bag isn't required.
static void unlock_without_bag(int item) {
    if ((item != IT_BOMB && item != IT_POWDER) || !rando_seed_dir()) return;
    bag_policy();
    uint32_t *inv = (uint32_t *)(uintptr_t)(GA(LIVE_SAVE) + 0x5568);
    if (item == IT_BOMB && !g_bag_bombs && !(*inv & (1u << 2))) {
        *inv |= 1u << 2;
        LOG("[pots] V12.5 first bombs: Bombs unlocked (Needs Bomb Bag is off)\n");
    }
    if (item == IT_POWDER && !g_bag_powder && !(*inv & (1u << 8))) {
        enum { GET_MAGIC_POWDER = 295 };
        *inv |= 1u << 8;
        if (!flags_ext_get(GET_MAGIC_POWDER)) flags_put(GET_MAGIC_POWDER, true);
        LOG("[pots] V12.5 first powder: Magic Powder unlocked (Needs Magic Powder Bag is off)\n");
    }
}

static int refill(Cpu *c, int item, int count) {
    unlock_without_bag(item);
    uint32_t inv = *(const uint32_t *)(uintptr_t)(GA(LIVE_SAVE) + 0x5568);
    int have = item == IT_BOMB ? inv & (1u << 2) : item == IT_POWDER ? inv & (1u << 8) : inv & (1u << 3);
    if (!have) {
        c->x[0] = IT_RUPEE_BLUE;
        c->x[1] = 1;
    }
    c->x[2] = (uint64_t)(int64_t)-1;  // the game's own add from here on
    LOG("[pots] refill item %d x%d: %s\n", item, count, have ? "owned -> refill" : "not owned -> blue rupee");
    return 0;
}

// Found Magic Powder (LAS shuffle-powder): LAS lets powder in only once the event flag GetMagicPowder is set - a
// powder capacity upgrade then adds 40 powder (chests, seashell rewards), the blue Skeletal Guards sell it. But every
// item-get sequence for the key MagicPowder sets that flag too (Items.gsheet gettingFlag), whatever it shows: a powder
// refill's "nowhere to put it" message and the Bomb Bag's (held up as the powder sack) set it without the bag, and
// the next capacity upgrade handed out real powder. In these seeds the flag stays clear while the powder bit
// (live save +0x5568 bit 8, set by the bag's own add) is - also mending saves where it was set that way.
static int g_shuffle_powder = -1;  // the running seed has it (read again after an in-place reboot)
RT_ON_RESET(shuffle_powder_reset) { g_shuffle_powder = -1; }

void rt_powder_flag_tick(void) {
    enum { GET_MAGIC_POWDER = 295 };  // (GlobalFlags.gsheet index)
    if (g_shuffle_powder < 0) {
        g_shuffle_powder = 0;
        const char *dir = rando_seed_dir();
        static char buf[1 << 16];
        char p[512];
        FILE *f = dir ? (snprintf(p, sizeof p, "%s/settings.json", dir), fopen(p, "rb")) : NULL;
        if (f) {
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            g_shuffle_powder = strstr(buf, "\"shuffle-powder\": true") != NULL;
        }
    }
    if (!g_shuffle_powder || !flags_ext_get(GET_MAGIC_POWDER)) return;
    if (*(const uint32_t *)(uintptr_t)(GA(LIVE_SAVE) + 0x5568) & (1u << 8)) return;  // (the Powder Bag is found)
    flags_put(GET_MAGIC_POWDER, false);
    LOG("[pots] GetMagicPowder set without the Powder Bag: cleared\n");
}

// x0 item, x1 count, x2 index: returns 1 when handled (AddItem then returns at once)
static int add_item_impl(Cpu *c) {
    int item = (int)(int32_t)c->x[0], count = (int)(int32_t)c->x[1], index = (int)(int32_t)c->x[2];
    extern int triforce_add_item(int item, int index);  // triforce.c: a Triforce piece (Triforce hunt)
    if (triforce_add_item(item, index)) return 1;
    extern int rt_shop_add_item(Cpu *);  // shopsanity.c: a stolen shopsanity good
    if (rt_shop_add_item(c)) return 1;
    if (index == REFILL_INDEX && (item == IT_BOMB || item == IT_POWDER || item == IT_ARROW)) return refill(c, item, count);
    if (count > 0 && index < 0) unlock_without_bag(item);  // V12.5: shop bombs / found powder in bagless seeds
    extern int rt_pet_item(int n);  // pets.c: companions found as items (tools/rando/pets.py PET_INDEX 90)
    if (index >= 90 && index < 93) return rt_pet_item(index - 90);
    if (index < 0 || index > 9 || index == 8) return 0;
    int rec = index == 9 ? 18 : 2 * index;
    uint8_t *r = (uint8_t *)(uintptr_t)(GA(LIVE_SAVE) + 0xE0 + 0x10 * rec);
    switch (item) {
    case IT_SMALL_KEY: r[0] = (uint8_t)(r[0] + (count > 0 ? count : 1)); break;
    case IT_COMPASS: r[1] |= 1; break;
    case IT_MAP: r[1] |= 2; break;
    case IT_BEAK: r[1] |= 4; break;
    case IT_NIGHTMARE_KEY: r[1] |= 8; break;
    default: return 0;
    }
    // (the get-item message names the dungeon: seed text, tools/rando/keysanity_text.py)
    LOG("[keysanity] item %d x%d -> dungeon %d (record %d: keys %d bits %#x)\n", item, count, index, rec, r[0], r[1]);
    return 1;
}

/* Observe the existing inner AddItem hook after silent/refill index normalization. */
int rt_add_item(Cpu *c) {
    extern void rt_silent_add(Cpu *);
    extern uint64_t lao_dev_item_begin(int,int,int);
    extern void lao_dev_item_end(uint64_t,int,int,int,int);
    rt_silent_add(c); // Same normalization order as before, before native policy dispatch.
    uint64_t transaction=lao_dev_item_begin((int)(int32_t)c->x[0],(int)(int32_t)c->x[1],(int)(int32_t)c->x[2]);
    int handled=add_item_impl(c);
    extern void laonline_coop_pickup(int,int,int);
    lao_dev_item_end(transaction,(int)(int32_t)c->x[0],(int)(int32_t)c->x[1],(int)(int32_t)c->x[2],handled);
    laonline_coop_pickup((int)(int32_t)c->x[0],(int)(int32_t)c->x[1],(int)(int32_t)c->x[2]);
    return handled;
}
