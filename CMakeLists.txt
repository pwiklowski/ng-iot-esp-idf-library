idf_component_register(SRCS "iot.c" "wifi.c" "config.c" "ota.c"
 		       INCLUDE_DIRS  "." 
 		       EMBED_TXTFILES ca_cert.pem
 		       REQUIRES nvs_flash app_update esp_http_client esp_websocket_client json esp_https_ota bt)

