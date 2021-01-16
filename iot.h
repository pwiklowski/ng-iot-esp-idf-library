#ifndef __IOT_H__
#define __IOT_H__

#include "esp_types.h"
#include "cJSON.h"

enum MessageType {
  Hello,
  GetValue,
  GetDevices,
  Notification,
  DeviceConnected,
  DeviceDisconnected,
  ValueUpdated,
  SetValue,
  GetDevice,
  DeviceListChanged,
  Error
};

typedef enum{
  MSG_STARTED,

  MSG_WS_CONNECTED,
  MSG_WS_DATA,
  MSG_WS_CLOSED,
  MSG_WS_CLOSE_UNAUTHORIZED,

  MSG_IOT_VALUE_UPDATED,

  MSG_TOKEN_REFRESHED
} IotEvent;

void iot_get_app_version(char* name, char* version);
void iot_get_device_uuid(char* uuid);

void iot_emit_event(IotEvent event_id, uint8_t* data, uint16_t data_len);
void iot_send_value_changed_notifcation(char *device_uuid, char *variable_uuid, cJSON *value);
void iot_create_variable_description(cJSON *vars, char *variable_uuid, char *name, char *access, char *schema, cJSON *value);
void iot_device_send_response(uint32_t req_id, cJSON* res);

void iot_init();

#endif // __IOT_H__
