#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "esp_types.h"

#define TOKEN_MAX_LEN 1500
#define REFRESH_TOKEN_MAX_LEN 500

typedef struct {
  char access_token[TOKEN_MAX_LEN];
  size_t access_token_length;
  char refresh_token[REFRESH_TOKEN_MAX_LEN];
  size_t refresh_token_length;
  uint64_t valid_to;

} Config_t;

void read_config(Config_t* config);
void save_config(Config_t* config);

#endif // __CONFIG_H__