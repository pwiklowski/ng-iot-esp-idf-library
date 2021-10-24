#include <string.h>

#include "config.h"
#include "nvs_flash.h"

void read_config(Config_t* config) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    printf("Reading from NVS ... ");

    memset((uint8_t *)config, 0, sizeof(Config_t));
    config->access_token_length = TOKEN_MAX_LEN;
    err = nvs_get_str(my_handle, "access_token", config->access_token, &config->access_token_length);
    config->refresh_token_length = REFRESH_TOKEN_MAX_LEN;
    err = nvs_get_str(my_handle, "refresh_token", config->refresh_token, &config->refresh_token_length);

    err = nvs_commit(my_handle);

    nvs_close(my_handle);
  }
}

void save_config(Config_t* config) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    err = nvs_set_str(my_handle, "access_token", config->access_token);
    err = nvs_set_str(my_handle, "refresh_token", config->refresh_token);

    err = nvs_commit(my_handle);
    nvs_close(my_handle);
  }
}
