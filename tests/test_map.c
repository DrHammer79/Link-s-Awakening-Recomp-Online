#include <assert.h>
#include <stdio.h>
#include "laonline_map_coords.h"
int main(void) {
    int c=-1,r=-1;
    assert(lao_field_cell(lao_name_id("Field"),lao_room_id("Field","Field_11C"), &c,&r));
    assert(c==2 && r==11);
    assert(lao_field_cell(lao_name_id("Field"),lao_room_id("Field","Field_05F"), &c,&r));
    assert(c==5 && r==5);
    assert(!lao_field_cell(lao_name_id("TownWell"),lao_room_id("Field","Field_11C"), &c,&r));
    assert(!lao_field_cell(lao_name_id("Field"),LAO_UNKNOWN, &c,&r));
    for(int row=0;row<16;row++)for(int col=0;col<16;col++){
      char name[20]; snprintf(name,sizeof name,"Field_%02d%c",row,'A'+col);
      int a=-1,b=-1;
      assert(lao_field_cell(lao_name_id("Field"),lao_room_id("Field",name),&a,&b));
      assert(a==col && b==row);
    }
    puts("PASS 256 Field hash-to-cell mapping and unknown-level fallback");
}
