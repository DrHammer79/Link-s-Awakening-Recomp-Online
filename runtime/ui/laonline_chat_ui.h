#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool laonline_chat_input_open(void);
uint64_t laonline_chat_input_generation(void);
void laonline_chat_input_close(void);
#ifdef __cplusplus
}
#endif
