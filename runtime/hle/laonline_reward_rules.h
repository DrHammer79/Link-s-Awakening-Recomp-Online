#pragma once
/* Receipt keys are exact-seed + reward kind + native collection index.
 * Repeatable resources, small keys, quest flags and unverified indices are excluded. */
/* Monotonic equipment only. Native 1.0.0 AddItem jump table audited at
 * main+8d08e0. No trade slots, refill items, bottles or quest flags. */
static inline int lao_equipment_bit(int item) {
 switch(item){case 0:case 1:return 0;case 2:case 3:return 1;
 case 5:return 3;case 6:return 4;case 7:return 5;case 8:return 6;
 case 9:return 7;case 12:return 9;case 13:case 14:return 10;
 case 15:return 11;case 16:return 12;default:return -1;}
}
static inline int lao_equipment_upgrade(int item){return item==1?1:item==3?2:item==14?4:0;}
static inline int lao_key_equipment_bit(int item){return item==20?0:item>=24&&item<=28?item-22:-1;}
static inline int lao_equipment(int item){return lao_equipment_bit(item)>=0 || lao_key_equipment_bit(item)>=0;}
static inline int lao_advanced_reward(int item){return (item>=50&&item<=57)||(item>=120&&item<=122);}
static inline int lao_reward_limit(int item) {
 if(lao_advanced_reward(item))return 1;
 if(lao_equipment(item))return 1;
 switch(item){case 22:return 50;case 58:return 32;case 59:return 9;
 case 44:case 45:case 47:case 49:return 10;default:return 0;}
}
static inline int lao_reward_valid(int item,int index) {
 int n=lao_reward_limit(item);return n && index>=0 && index<n &&
 ((item!=44 && item!=45 && item!=47 && item!=49) || index!=8);
}
static inline int lao_reward_dungeon_bit(int item){
 switch(item){case 44:return 1;case 45:return 2;case 47:return 4;case 49:return 8;default:return 0;}
}
#define LAO_REWARD_QUERY 0x4c414f5bu
#define LAO_REWARD_REPLY 0x4c414f5cu
#define LAO_REWARD_SEND 0x13u
#define LAO_REWARD_RECEIVE 0x97u
