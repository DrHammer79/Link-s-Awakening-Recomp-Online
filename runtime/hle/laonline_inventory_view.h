#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LAO_INVENTORY_ICONS 31
static const int lao_inventory_icons[LAO_INVENTORY_ICONS]={0,1,2,3,5,6,7,8,9,12,13,14,15,16,20,24,25,26,27,28,50,51,52,53,54,55,56,57,120,121,122};
typedef struct {uint8_t owned[LAO_INVENTORY_ICONS];uint8_t shells,pieces,containers,shell_total;int valid;} LaoInventoryView;
int laonline_inventory_view(LaoInventoryView *out);
#ifdef __cplusplus
}
#endif
