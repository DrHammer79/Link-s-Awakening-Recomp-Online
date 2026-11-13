#pragma once
#include <stdint.h>
void lao_dev_tick(uint64_t frame);
void lao_dev_item(int item,int count,int index,int handled);
uint64_t lao_dev_item_begin(int item,int count,int index);
void lao_dev_item_end(uint64_t transaction,int item,int count,int index,int handled);
void lao_dev_remote(int active);
void lao_dev_save(const char *path,int success);
