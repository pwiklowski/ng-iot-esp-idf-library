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
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_transport_ws.h"
#include "esp_http_client.h"

#include "cJSON.h"
#include "config.h"
#include "ota.h"
#include "esp_ota_ops.h"

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_device.h"

#include "esp_tls.h"



#define DEVICE_UUID_TEMPLATE "%02x%02x%02x%02x-%02x%02x-40b4-b336-8a36f879111e"

extern void iot_device_event_handler(const char *payload, const size_t len);
extern void iot_device_init();
extern void iot_device_deinit();

extern char* audience;
extern char* scope;
extern char* AUTH_TOKEN_URL;
extern char* AUTH_CODE_URL;
extern char* IOT_SERVER_URL_TEMPLATE;
extern int SOFTWARE_UPDATE_CHECK_INTERVAL_MIN;
extern int TOKEN_REFRESH_INTERVAL_MIN;
extern char* CLIENT_ID;
extern char* CLIENT_SECRET;


void websocket_open();
void websocket_close();
void iot_refresh_token();
void iot_login();
void iot_start();


Config_t config;

const char *grant_type = "refresh_token";
static const char *TAG = "WEBSOCKET";
#define BUF_LEN 4096

static char buf[BUF_LEN];
static uint8_t manufacturer_data[11];

esp_websocket_client_handle_t client;

MessageBufferHandle_t xMessageBuffer;

TimerHandle_t timer;
TimerHandle_t ota_timer;


void iot_get_app_version(char* name, char* version) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;
  if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
      sprintf(version, "%s", running_app_info.version);
      sprintf(name, "%s", running_app_info.project_name);
  }
}

void iot_get_device_uuid(char* uuid) {
  uint8_t chipid[6];
  esp_read_mac(chipid, ESP_MAC_WIFI_STA);
  sprintf(uuid, DEVICE_UUID_TEMPLATE, chipid[0], chipid[1], chipid[2], chipid[3], chipid[4], chipid[5]);
}


void iot_emit_event(IotEvent event_id, uint8_t *data, uint16_t data_len) {
  uint8_t message[data_len + 1];
  message[0] = event_id;
  memcpy(&message[1], data, data_len);
  xMessageBufferSend(xMessageBuffer, message, sizeof(message), 100 / portTICK_PERIOD_MS);
}

void iot_create_variable_description(cJSON *vars, char *variable_uuid, char *name, char *access, char *schema, cJSON *value) {
  cJSON *variable = cJSON_CreateObject();

  cJSON_AddStringToObject(variable, "name", name);
  cJSON_AddStringToObject(variable, "access", access);
  cJSON_AddRawToObject(variable, "schema", schema);
  cJSON_AddItemToObject(variable, "value", value);

  cJSON_AddItemToObject(vars, variable_uuid, variable);
}

void iot_send_value_changed_notifcation(char *device_uuid, char *variable_uuid, cJSON *value) {
  cJSON *notification = cJSON_CreateObject();

  cJSON_AddNumberToObject(notification, "type", 6);

  cJSON *args = cJSON_AddObjectToObject(notification, "args");

  cJSON_AddStringToObject(args, "deviceUuid", device_uuid);
  cJSON_AddStringToObject(args, "variableUuid", variable_uuid);
  cJSON_AddItemToObject(args, "value", cJSON_Duplicate(value, true));

  char *notification_string = cJSON_Print(notification);

  iot_emit_event(MSG_IOT_VALUE_UPDATED, (uint8_t*) notification_string, strlen(notification_string));

  cJSON_Delete(notification);

  free(notification_string);
}

void iot_device_send_response(uint32_t req_id, cJSON* res) {
   cJSON *response= cJSON_CreateObject();
   cJSON_AddNumberToObject(response, "resId", req_id);
   cJSON_AddItemToObject(response, "res",  res);

   char *request_string = cJSON_Print(response);

   esp_websocket_client_send(client, request_string, strlen(request_string), 500);
   cJSON_Delete(response);
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
  char *access_token = cJSON_GetObjectItem(json, "id_token")->valuestring;
  char *refresh_token = cJSON_GetObjectItem(json, "refresh_token")->valuestring;

  memcpy(config.access_token, access_token, strlen(access_token)+1);
  config.access_token_length = strlen(access_token);
  memcpy(config.refresh_token, refresh_token, strlen(refresh_token)+1);
  config.refresh_token_length = strlen(refresh_token);

  save_config(&config);

  cJSON_Delete(json);

  iot_emit_event(MSG_TOKEN_REFRESHED, 0, 0);
}

void iot_refresh_token() {
  if (strlen(config.refresh_token) == 0) {
    ESP_LOGI(TAG, "No refresh token. Login");
    iot_login();
    return;
  }

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


static esp_ble_adv_params_t advParams = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

void iot_start_ble_adv() {
  esp_ble_adv_data_t advData = {0};
  advData.p_manufacturer_data = &manufacturer_data[0];
  advData.manufacturer_len = 11;

  advData.flag = ESP_BLE_ADV_FLAG_NON_LIMIT_DISC;
  advData.include_name = true;
  advData.min_interval = 0x0100;
  advData.max_interval = 0x0100;
  esp_ble_gap_config_adv_data(&advData);

  esp_ble_gap_start_advertising(&advParams);
}

void iot_stop_ble_adv() {
  esp_ble_gap_stop_advertising();
}

void iot_handle_login_response(char* payload, size_t size) {
  cJSON *json = cJSON_Parse((char *)payload);

  char *device_code = cJSON_GetObjectItem(json, "device_code")->valuestring;
  char *verification_uri_complete = cJSON_GetObjectItem(json, "verification_uri_complete")->valuestring;
  int interval = cJSON_GetObjectItem(json, "interval")->valueint;

  ESP_LOGI(TAG, "\n\n\nLogin using this link: %s\n\n\n", verification_uri_complete);

  uint8_t index = strlen(verification_uri_complete)-11;

  memcpy(manufacturer_data, &verification_uri_complete[index], 11);

  iot_start_ble_adv();

  for(uint16_t i=0; i<1000; i++) {
    int response_code = iot_check_login_response(device_code);
    ESP_LOGI(TAG, "Response %d",response_code); 

    if (response_code == 200) {
      iot_handle_token_update(buf);
      iot_stop_ble_adv();
      return;
    }

    vTaskDelay(10000 / portTICK_PERIOD_MS); // Add timout, use response to calculate interval
  }

  iot_stop_ble_adv();
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
      if (!esp_websocket_client_is_connected(client)) {
        websocket_open();
      }


      break;
    case MSG_WS_CONNECTED:

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

  esp_err_t ret;

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret) {
      ESP_LOGE(TAG, "%s initialize controller failed\n", __func__);
      return;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret) {
      ESP_LOGE(TAG, "%s enable controller failed\n", __func__);
      return;
  }

  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_ble_gap_set_device_name("iot");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N14);


  iot_device_init();
  
  xMessageBuffer = xMessageBufferCreate(1000);
  if (xMessageBuffer == NULL) {
    assert(true);
  }

  uint8_t buffer[512];
  uint8_t message[] = {MSG_STARTED};
  xMessageBufferSend(xMessageBuffer, message, sizeof(message), 100 / portTICK_PERIOD_MS);


  timer = xTimerCreate("refreshTokenTimer", (TOKEN_REFRESH_INTERVAL_MIN*60*1000) / portTICK_PERIOD_MS, pdTRUE
      , (void*)1, iot_refresh_token);
  xTimerStart(timer, 0);

  while (1) {
    size_t xReceivedBytes = xMessageBufferReceive(xMessageBuffer, buffer, sizeof(buffer), 100 / portTICK_PERIOD_MS);
    if (xReceivedBytes > 0) {
      iot_handle_event(buffer[0], &buffer[1], xReceivedBytes - 1);
    }
  }

  iot_device_deinit();
}

void check_updates() {
  xTaskCreate(ota_task, "ota_task", 8192, NULL, ESP_TASK_MAIN_PRIO + 1, NULL);
}

void iot_init() {
  xTaskCreate(iot_start, "iot_start", 8192, NULL, ESP_TASK_MAIN_PRIO + 1, NULL);

  check_updates();
  ota_timer = xTimerCreate("check_updates", (SOFTWARE_UPDATE_CHECK_INTERVAL_MIN*60*1000) / portTICK_PERIOD_MS, pdTRUE , (void*)1, check_updates);
  xTimerStart(ota_timer, 0);
}
