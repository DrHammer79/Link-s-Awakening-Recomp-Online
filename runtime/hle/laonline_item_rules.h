#pragma once
#include <stddef.h>
/* Explicit 1.0.0 itemID/name mapping verified from Items.gsheet. Rows != itemIDs. */
static inline const char *lao_pickup_name(int id) { switch(id) {
case 0: return "Sword Lv1";
case 1: return "Sword Lv2";
case 2: return "Shield";
case 3: return "Mirror Shield";
case 4: return "Bomb";
case 5: return "Bow";
case 6: return "Hook Shot";
case 7: return "Boomerang";
case 8: return "Magic Rod";
case 9: return "Shovel";
case 10: return "Sleepy Mushroom";
case 11: return "Magic Powder";
case 12: return "Rocs Feather";
case 13: return "Power Bracelet Lv1";
case 14: return "Power Bracelet Lv2";
case 15: return "Pegasus Boots";
case 16: return "Ocarina";
case 17: return "Clothes Green";
case 18: return "Clothes Red";
case 19: return "Clothes Blue";
case 20: return "Flippers";
case 21: return "Secret Medicine";
case 22: return "Seashell";
case 23: return "Golden Leaf";
case 24: return "Tail Key";
case 25: return "Slime Key";
case 26: return "Angler Key";
case 27: return "Face Key";
case 28: return "Bird Key";
case 29: return "Yoshi Doll";
case 30: return "Ribbon";
case 31: return "Dog Food";
case 32: return "Bananas";
case 33: return "Stick";
case 34: return "Honeycomb";
case 35: return "Pineapple";
case 36: return "Hibiscus";
case 37: return "Letter";
case 38: return "Broom";
case 39: return "Fishing Hook";
case 40: return "Pink Bra";
case 42: return "Mermaids Scale";
case 43: return "Magnifying Lens";
case 44: return "Compass";
case 45: return "Dungeon Map";
case 47: return "Stone Beak";
case 48: return "Small Key";
case 49: return "Nightmare Key";
case 50: return "Full Moon Cello";
case 51: return "Conch Horn";
case 52: return "Sea Lilys Bell";
case 53: return "Surf Harp";
case 54: return "Wind Marimba";
case 55: return "Coral Triangle";
case 56: return "Evening Calm Organ";
case 57: return "Thunder Drum";
case 58: return "Heart Piece";
case 59: return "Heart Container";
case 60: return "Arrow";
case 100: return "Heart";
case 101: return "Rupee1";
case 102: return "Rupee5";
case 104: return "Rupee20";
case 105: return "Rupee50";
case 106: return "Rupee100";
case 108: return "Rupee300";
case 110: return "Fairy";
case 111: return "Defence Up";
case 112: return "Power Up";
case 61: return "Apple";
case 62: return "Green Apple";
case 113: return "Bottle";
case 120: return "Song Wind Fish";
case 121: return "Song Mambo";
case 122: return "Song Soul";
case 123: return "Magic Powder Max Up";
case 124: return "Bomb Max Up";
case 125: return "Arrow Max Up";
case 114: return "Panel Dungeon Piece";
case 115: return "Panel Dungeon Piece Set";
case 116: return "Shell Rader";
case 126: return "Bottle Fairy";
case 117: return "Panel Dungeon Plus Chip";
default:return NULL;} }
#define LAO_PROGRESS_FLAG_COUNT 165
static const unsigned short lao_progress_flags[] = {
20, /* TreasureBoxOpenField02E */
21, /* TreasureBoxOpenField02I */
22, /* TreasureBoxOpenField02N */
23, /* TreasureBoxOpenField04E */
24, /* TreasureBoxOpenField06M */
25, /* TreasureBoxOpenField06N */
26, /* TreasureBoxOpenField07M */
27, /* TreasureBoxOpenField08B */
28, /* TreasureBoxOpenField15F */
100, /* TailCaveShutterOpened */
101, /* KeyCavernShutterOpened */
102, /* FaceShrineOpened */
103, /* EaglesTowerOpened */
105, /* AnglersWaterfallOpened */
106, /* MarthaStatueOpened */
107, /* WeathercockOpened */
349, /* Lv1InstrumentGet */
350, /* Lv2InstrumentGet */
351, /* Lv3InstrumentGet */
352, /* Lv4InstrumentGet */
353, /* Lv5InstrumentGet */
354, /* Lv6InstrumentGet */
355, /* Lv7InstrumentGet */
356, /* Lv8InstrumentGet */
500, /* TBOX_OPEN_L01_05A */
501, /* TBOX_OPEN_L01_05D */
502, /* TBOX_OPEN_L01_05F */
503, /* TBOX_OPEN_L01_06B */
504, /* TBOX_OPEN_L01_06C */
505, /* TBOX_OPEN_L01_06D */
506, /* TBOX_OPEN_L01_07D */
507, /* TBOX_OPEN_L01_07E */
508, /* TBOX_OPEN_L01_08B */
510, /* TBOX_OPEN_L02_02B */
511, /* TBOX_OPEN_L02_02C */
512, /* TBOX_OPEN_L02_02D */
513, /* TBOX_OPEN_L02_03C */
514, /* TBOX_OPEN_L02_03F */
515, /* TBOX_OPEN_L02_06B */
516, /* TBOX_OPEN_L02_08C */
517, /* TBOX_OPEN_L02_08D */
518, /* TBOX_OPEN_L02_08E */
519, /* TBOX_OPEN_L02_08F */
520, /* TBOX_OPEN_L03_01C */
521, /* TBOX_OPEN_L03_02A */
522, /* TBOX_OPEN_L03_02C */
523, /* TBOX_OPEN_L03_02D */
524, /* POP_ITEM_GET_L03_03G */
525, /* TBOX_OPEN_L03_04B */
526, /* POP_ITEM_GET_L03_04F */
527, /* TBOX_OPEN_L03_05B */
528, /* TBOX_OPEN_L03_06B */
529, /* TBOX_OPEN_L03_06C */
530, /* TBOX_OPEN_L03_07B */
531, /* TBOX_OPEN_L03_08C */
532, /* TBOX_OPEN_L04_02D */
533, /* TBOX_OPEN_L04_03G */
534, /* TBOX_OPEN_L04_04D */
535, /* POP_ITEM_GET_L04_04E */
536, /* TBOX_OPEN_L04_04F */
537, /* TBOX_OPEN_L04_05C */
538, /* TBOX_OPEN_L04_05D */
539, /* TBOX_OPEN_L04_05G */
540, /* TBOX_OPEN_L04_06F */
541, /* TBOX_OPEN_L04_07C */
542, /* TBOX_OPEN_L04_07E */
543, /* TBOX_OPEN_L04_07F */
544, /* TBOX_OPEN_L04_08E */
545, /* TBOX_OPEN_L05_01E */
546, /* TBOX_OPEN_L05_02E */
547, /* TBOX_OPEN_L05_02G */
548, /* TBOX_OPEN_L05_03G */
549, /* TBOX_OPEN_L05_03H */
550, /* TBOX_OPEN_L05_05G */
551, /* TBOX_OPEN_L05_06C */
552, /* POP_ITEM_GET_L05_07D */
553, /* TBOX_OPEN_L05_08E */
554, /* TBOX_OPEN_L05_08G */
555, /* TBOX_OPEN_L06_02A */
556, /* TBOX_OPEN_L06_02H */
557, /* TBOX_OPEN_L06_03B */
558, /* TBOX_OPEN_L06_03G */
559, /* TBOX_OPEN_L06_04B */
560, /* TBOX_OPEN_L06_04G */
561, /* TBOX_OPEN_L06_05A */
562, /* TBOX_OPEN_L06_06C */
563, /* TBOX_OPEN_L06_07B */
564, /* TBOX_OPEN_L06_07C */
565, /* TBOX_OPEN_L06_07G */
566, /* POP_ITEM_GET_L06_08G */
567, /* TBOX_OPEN_L07_01B */
568, /* TBOX_OPEN_L07_01C */
569, /* TBOX_OPEN_L07_03D */
570, /* TBOX_OPEN_L07_04B */
571, /* TBOX_OPEN_L07_05A */
572, /* TBOX_OPEN_L07_05D */
573, /* TBOX_OPEN_L07_05G */
574, /* TBOX_OPEN_L07_06H */
575, /* TBOX_OPEN_L07_07A */
576, /* TBOX_OPEN_L08_02A */
577, /* TBOX_OPEN_L08_02E */
578, /* TBOX_OPEN_L08_02H */
579, /* TBOX_OPEN_L08_03C */
580, /* TBOX_OPEN_L08_03F */
581, /* TBOX_OPEN_L08_04B */
582, /* TBOX_OPEN_L08_05B */
583, /* TBOX_OPEN_L08_06B */
584, /* TBOX_OPEN_L08_06D */
585, /* TBOX_OPEN_L08_07B */
586, /* TBOX_OPEN_L08_07F */
587, /* TBOX_OPEN_L08_08A */
588, /* TBOX_OPEN_L08_08H */
589, /* TBOX_OPEN_L10_04F */
590, /* TBOX_OPEN_L10_05D */
591, /* TBOX_OPEN_L10_07D */
592, /* TBOX_OPEN_L10_07F */
593, /* TBOX_OPEN_L10_08E */
594, /* POP_ITEM_GET_MysteriousWC01_01A */
595, /* TBOX_OPEN_MysteriousWC01_02B */
596, /* TBOX_OPEN_MysteriousWC02_01A */
597, /* POP_ITEM_GET_None */
598, /* TBOX_OPEN_RichardCave_01A */
599, /* POP_ITEM_GET_SeashellHouse_01A */
600, /* TBOX_OPEN_Tamaranch02_01B */
602, /* POP_ITEM_GET_TamaranchFoot_01C */
603, /* TBOX_OPEN_TamaranchFoot_01D */
604, /* POP_ITEM_GET_TownWell_01A */
605, /* TBOX_OPEN_UkukuCave01_01A */
606, /* TBOX_OPEN_UkukuCave02_02A */
607, /* POP_ITEM_GET_AnimalVillageCave_02A */
608, /* TBOX_OPEN_ArmosShrineCave_01A */
610, /* TBOX_OPEN_DreamShrine_01B */
611, /* TBOX_OPEN_GopongaCave_01A */
612, /* TBOX_OPEN_GopongaCave_01B */
613, /* TBOX_OPEN_KanaletCastleSouthC_01A */
614, /* TBOX_OPEN_MoriblinCave_01A */
615, /* TBOX_OPEN_MoriblinCave_01B */
616, /* TBOX_OPEN_MoriblinCave_02A */
617, /* TBOX_OPEN_Tamaranch05_04A_1 */
618, /* TBOX_OPEN_Tamaranch05_04A_2 */
619, /* TBOX_OPEN_Tamaranch05_04A_3 */
620, /* TBOX_OPEN_Tamaranch05_04A_4 */
621, /* TBOX_OPEN_Tamaranch05_04A_5 */
776, /* KeyGet_L01_08C */
787, /* KeyGet_L02_7D */
788, /* KeyGet_L02_7F */
807, /* KeyGet_L03_01B */
813, /* KeyGet_L03_03A */
818, /* KeyGet_L03_03G */
820, /* KeyGet_L03_04C */
822, /* KeyGet_L03_04F */
825, /* KeyGet_L03_05G */
830, /* KeyGet_L03_08G */
833, /* KeyGet_L04_06A */
881, /* KeyGet_L05_01C */
898, /* KeyGet_L06_03D */
899, /* KeyGet_L06_05D */
951, /* KeyGet_L07_04A */
955, /* KeyGet_L07_08D */
965, /* KeyGet_L08_07G */
967, /* KeyGet_L08_03G */
969, /* KeyGet_L08_04C */
971, /* KeyGet_L08_06A */
1012, /* KeyGet_L10_05E */
1014, /* KeyGet_L10_05F */
};
