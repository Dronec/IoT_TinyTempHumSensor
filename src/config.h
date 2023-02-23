#ifndef Config_h_
#define Config_h_

// Custom defs
#include "C:\Dev\Arduino\libraries\MyCustomStaticDefinitions\DefsWiFi.h"
#include "C:\Dev\Arduino\libraries\MyCustomStaticDefinitions\DefsIoT.h"
// OTA values
#define OTA_SERVER_URL "http://miniserverhome.local"
#define OTA_GET_VERSION_ENDPOINT "/version.php?device="
#define OTA_GET_FIRMWARE_ENDPOINT "/firmware.php?device="
#define LOG_UPLOAD_ENDPOINT "/upload.php?device="
#define FW_VERSION "1016"

// IOT values
//#define IOTID TestDeviceID
//#define IOTKEY TestDeviceKey
//#define IOTID EnvSensorOutdoorID
//#define IOTKEY EnvSensorOutdoorKey
#define IOTID EnvSensorMicroID
#define IOTKEY EnvSensorMicroKey
#define TimeToSleepInSeconds 600

// WiFi
#define SSIDNAME WIFISSID_2
#define SSIDKEY WIFIPASS_2

#endif

// Debug
//#define debugon