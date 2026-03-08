#pragma once

// Copy AppBridgeSecrets.example.h to AppBridgeSecrets.h and fill your values.
// AppBridgeSecrets.h is ignored via .gitignore.
#if __has_include("AppBridgeSecrets.h")
#include "AppBridgeSecrets.h"
#endif

#ifndef APP_DEVICE_ID
#define APP_DEVICE_ID "power_light_v1_banco"
#endif

#ifndef APP_MQTT_TOPIC_BASE
#define APP_MQTT_TOPIC_BASE "home/power_light_v1_banco"
#endif

#ifndef APP_HA_DISCOVERY_PREFIX
#define APP_HA_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef APP_HA_ENABLE_DISCOVERY
#define APP_HA_ENABLE_DISCOVERY 1
#endif

#ifndef APP_HA_DEVICE_NAME
#define APP_HA_DEVICE_NAME "POWER LIGHT V1"
#endif

#ifndef APP_HA_DEVICE_MODEL
#define APP_HA_DEVICE_MODEL "XIAO ESP32-C6 + PZEM004T"
#endif

#ifndef APP_HA_DEVICE_MANUFACTURER
#define APP_HA_DEVICE_MANUFACTURER "DIY"
#endif

#ifndef APP_HA_SW_VERSION
#define APP_HA_SW_VERSION "1.0.0"
#endif

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif

#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD ""
#endif

#ifndef APP_MQTT_HOST
#define APP_MQTT_HOST ""
#endif

#ifndef APP_MQTT_PORT
#define APP_MQTT_PORT 1883
#endif

#ifndef APP_MQTT_USERNAME
#define APP_MQTT_USERNAME ""
#endif

#ifndef APP_MQTT_PASSWORD
#define APP_MQTT_PASSWORD ""
#endif
