#include "iot.h"

#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_transport_ws.h"
#include "esp_http_client.h"

#include "cJSON.h"
#include "config.h"

#include "secrets.h"

extern char *iot_device_get_description();
extern void iot_device_event_handler(const char *payload, const size_t len);
extern void iot_device_init();
extern void iot_device_deinit();


void websocket_open();
void websocket_close();
void iot_refresh_token();
void iot_login();
void iot_start();


Config_t config;

static const char *TAG = "WEBSOCKET";
#define BUF_LEN 4096
char buf[BUF_LEN];

esp_websocket_client_handle_t client;

MessageBufferHandle_t xMessageBuffer;

const char *audience = "https%3A%2F%2Fwiklosoft.eu.auth0.com%2Fapi%2Fv2%2F";
const char *scope = "profile+openid+offline_access";

const char* AUTH_TOKEN_URL = "https://wiklosoft.eu.auth0.com/oauth/token";
const char *AUTH_CODE_URL = "https://wiklosoft.eu.auth0.com/oauth/device/code";

//const char* IOT_SERVER_URL_TEMPLATE = "ws://192.168.1.28:8000/device?token=%s";
const char *IOT_SERVER_URL_TEMPLATE = "wss://iot.wiklosoft.com/connect/device?token=%s";

void iot_emit_event(IotEvent event_id, uint8_t *data, uint16_t data_len) {
  uint8_t message[data_len + 1];
  message[0] = event_id;
  memcpy(&message[1], data, data_len);
  xMessageBufferSend(xMessageBuffer, message, sizeof(message), 100 / portTICK_PERIOD_MS);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
    iot_emit_event(MSG_WS_CONNECTED, 0, 0);
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED %d", data->op_code);
    break;
  case WEBSOCKET_EVENT_DATA:
    if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) { // text frame
      iot_emit_event(MSG_WS_DATA, data->data_ptr, data->data_len);
    } else if (data->op_code == WS_TRANSPORT_OPCODES_CLOSE) { // text frame
      uint16_t code = data->data_ptr[0] << 8 | data->data_ptr[1];
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CLOSED %d", code);
      iot_emit_event(MSG_WS_CLOSE_UNAUTHORIZED, 0, 0);
    }
    break;
  case WEBSOCKET_EVENT_CLOSED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_CLOSED");
    break;
  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
    break;
  }
}

void websocket_open(void) {
  sprintf(buf, IOT_SERVER_URL_TEMPLATE, config.access_token);

  esp_websocket_client_config_t websocket_cfg = {
      .uri = buf,
      .pingpong_timeout_sec = 10
  };

  ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

  client = esp_websocket_client_init(&websocket_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);
}

void websocket_close(void) {
  esp_websocket_client_close(client, 100);
  esp_websocket_client_destroy(client);
}

int iot_post(char* url, char* post_data, uint16_t post_data_len, int* response_len) {
  esp_http_client_config_t config = {
      .url = url,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  ESP_LOGI(TAG, "make POST request: %s", post_data);

  esp_http_client_set_url(client, url);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

  esp_err_t err = esp_http_client_open(client, post_data_len);
  if (err == ESP_OK) {
    esp_http_client_write(client, post_data, post_data_len);
    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    *response_len = esp_http_client_read(client, buf, BUF_LEN);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "HTTP POST success: %d %s", status_code, buf);
    return status_code;
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }

  return -1;
}

void iot_handle_token_update(char* payload) {
  cJSON *json = cJSON_Parse((char *)payload);
  char *access_token = cJSON_GetObjectItem(json, "access_token")->valuestring;
  char *refresh_token = cJSON_GetObjectItem(json, "refresh_token")->valuestring;

  memcpy(config.access_token, access_token, strlen(access_token)+1);
  config.access_token_length = strlen(access_token);
  memcpy(config.refresh_token, refresh_token, strlen(refresh_token)+1);
  config.refresh_token_length = strlen(refresh_token);

  save_config(&config);

  iot_emit_event(MSG_TOKEN_REFRESHED, 0, 0);
}

void iot_refresh_token() {
  ESP_LOGI(TAG, "Refresh token");

  const char *grant_type = "refresh_token";
  size_t post_data_len = sprintf(buf, "client_id=%s&client_secret=%s&grant_type=%s&refresh_token=%s", CLIENT_ID, CLIENT_SECRET, grant_type, config.refresh_token);

  int response_len;
  int res = iot_post(AUTH_TOKEN_URL, buf, post_data_len, &response_len);
  if (res == 200) {
    iot_handle_token_update(buf);
    ESP_LOGI(TAG, "Token refreshed");
  } else {
    iot_login();
  }
}

int iot_check_login_response(char* device_code) {
  const char *grant_type = "urn:ietf:params:oauth:grant-type:device_code";
  size_t post_data_len = sprintf(buf, "client_id=%s&device_code=%s&grant_type=%s", CLIENT_ID, device_code, grant_type);

  int response_len;
  return iot_post(AUTH_TOKEN_URL, buf, post_data_len, &response_len);
}

void iot_handle_login_response(char* payload, size_t size) {
  cJSON *json = cJSON_Parse((char *)payload);

  char *device_code = cJSON_GetObjectItem(json, "device_code")->valuestring;
  char *verification_uri_complete = cJSON_GetObjectItem(json, "verification_uri_complete")->valuestring;
  int interval = cJSON_GetObjectItem(json, "interval")->valueint;

  ESP_LOGI(TAG, "\n\n\nLogin using this link: %s\n\n\n", verification_uri_complete);

  for(uint16_t i=0; i<1000; i++) {
    int response_code = iot_check_login_response(device_code);
    ESP_LOGI(TAG, "Response %d",response_code); 

    if (response_code == 200) {
      iot_handle_token_update(buf);
      return;
    }

    vTaskDelay(10000 / portTICK_PERIOD_MS); // Add timout, use response to calculate interval
  }
}

void iot_login() {
  size_t post_data_len = sprintf(buf, "client_id=%s&audience=%s&scope=%s", CLIENT_ID, audience, scope);

  int response_len;
  int res = iot_post(AUTH_CODE_URL, buf, post_data_len, &response_len);
  if (res == 200) {
    iot_handle_login_response(buf, response_len);
  }
}

void iot_handle_event(IotEvent event, const uint8_t* data, const uint16_t data_len) {
  char* description;
  switch(event) {
    case MSG_STARTED:
      if (config.access_token_length == 0) {
        iot_login();
      } else {
        iot_refresh_token();
      }
      break;
    case MSG_TOKEN_REFRESHED:
      websocket_open();
      break;
    case MSG_WS_CONNECTED:
      description = iot_device_get_description();
      esp_websocket_client_send(client, description, strlen(description), 500);
      free(description);
      break;
    case MSG_WS_DATA:
      iot_device_event_handler((const char *)data, data_len);
      break;
    case MSG_WS_CLOSE_UNAUTHORIZED:
      websocket_close();
      iot_refresh_token();
      break;
    case MSG_WS_CLOSED:
      websocket_close();
      break;
    case MSG_IOT_VALUE_UPDATED:
      esp_websocket_client_send(client, (char*) data, data_len, 500);
      break;
  }
}

void iot_start() {
  read_config(&config);

  iot_device_init();
  
  xMessageBuffer = xMessageBufferCreate(1000);
  if (xMessageBuffer == NULL) {
    assert(true);
  }

  uint8_t buffer[512];
  uint8_t message[] = {MSG_STARTED};
  xMessageBufferSend(xMessageBuffer, message, sizeof(message), 100 / portTICK_PERIOD_MS);

  while (1) {
    size_t xReceivedBytes = xMessageBufferReceive(xMessageBuffer, buffer, sizeof(buffer), 100 / portTICK_PERIOD_MS);
    if (xReceivedBytes > 0) {
      iot_handle_event(buffer[0], &buffer[1], xReceivedBytes - 1);
    }
  }

  iot_device_deinit();
}

void iot_init() {
  xTaskCreate(iot_start, "iot_start", 8192, NULL, ESP_TASK_MAIN_PRIO + 1, NULL);
}
