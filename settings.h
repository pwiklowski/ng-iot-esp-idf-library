/*
 * settings.h
 *
 *  Created on: Jan 5, 2021
 *      Author: pwiklowski
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_


#define audience "https%3A%2F%2Fwiklosoft.eu.auth0.com%2Fapi%2Fv2%2F"
#define scope "name+email+profile+openid+offline_access"

#define AUTH_TOKEN_URL  "https://wiklosoft.eu.auth0.com/oauth/token"
#define AUTH_CODE_URL  "https://wiklosoft.eu.auth0.com/oauth/device/code"

#define OTA_IMAGE_URL "https://ota.wiklosoft.com/iot-rgb-led-driver-esp32.bin"

#define IOT_SERVER_URL_TEMPLATE  "ws://192.168.1.28:8000/device?token=%s"
//#define IOT_SERVER_URL_TEMPLATE "wss://iot.wiklosoft.com/connect/device?token=%s"

#define SOFTWARE_UPDATE_CHECK_INTERVAL_MIN 30

#define TOKEN_REFRESH_INTERVAL_MIN 12*60

#define WIFI_SSID ""
#define WIFI_PASS ""

#define CLIENT_ID ""
#define CLIENT_SECRET ""


#endif /* MAIN_IOT_SETTINGS_H_ */
