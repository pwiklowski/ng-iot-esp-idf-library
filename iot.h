#ifndef __IOT_H__
#define __IOT_H__

#include "esp_types.h"

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

void iot_emit_event(IotEvent event_id, uint8_t* data, uint16_t data_len);

void iot_init();

#endif // __IOT_H__