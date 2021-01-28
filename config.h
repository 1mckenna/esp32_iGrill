//Configuration Settings for the ESP32 iGrill BLE Client

// Set Wifi Manager Logging Level
//Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _WIFIMGR_LOGLEVEL_    1

// Enable iGrill BLE Client Debugging which will print detailed information to Serial Port
// Level 0: Print only Basic Info (Default)
// Level 1: Level 0 + Print BLE Connection Info
// Level 2: Level 1 + Print everything including temp/batt callbacks (Only Recommended for troubleshooting BLE issues)
#define IGRILL_DEBUG_LVL 1
// Setup File System Information (Needed to Read and Store the JSON )
#define FileFS        LITTLEFS
#define FS_Name       "LittleFS"
// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false
#define MIN_AP_PASSWORD_SIZE       8
#define SSID_MAX_LEN              32
#define PASS_MAX_LEN              64

#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
#define LED_BUILTIN       2
#define LED_ON            HIGH
#define LED_OFF           LOW

// Configuration Settings for ESP Double Reset Detector (DRD)
#define ESP_DRD_USE_LITTLEFS    true
#define ESP_DRD_USE_SPIFFS      false
#define ESP_DRD_USE_EEPROM      false
#define DOUBLERESETDETECTOR_DEBUG       false  //Enable or Disable DRD Debugging
#define DRD_TIMEOUT 5 // Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0 // RTC Memory Address for the DoubleResetDetector to use


// Default configuration values for MQTT
#define MQTT_SERVER              "127.0.0.1"
#define MQTT_SERVERPORT          "1883"
#define MQTT_USERNAME            "mqtt"
#define MQTT_PASSWORD            "password"
#define MQTT_BASETOPIC           "igrill" //If you are using Home Assistant you should set this to your discovery_prefix (default: homeassistant)

// Labels and max field lengths MQTT Custom parameters used in the Configuration Portal
#define MQTT_SERVER_Label             "MQTT_SERVER_Label"
#define MQTT_SERVERPORT_Label         "MQTT_SERVERPORT_Label"
#define MQTT_USERNAME_Label           "MQTT_USERNAME_Label"
#define MQTT_PASSWORD_Label           "MQTT_PASSWORD_Label"
#define MQTT_BASETOPIC_Label           "MQTT_BASETOPIC_Label"
// MQTT Field Lengths (Change the Lengths here if the field isnt large enough)
#define custom_MQTT_SERVER_LEN       20
#define custom_MQTT_PORT_LEN          5
#define custom_MQTT_USERNAME_LEN     20
#define custom_MQTT_PASSWORD_LEN     40
#define custom_MQTT_BASETOPIC_LEN    40



// Number of Wifi Credentials to Store
#define NUM_WIFI_CREDENTIALS      2
#define  CONFIG_FILENAME          F("/wifi_cred.dat")
// Use false if you don't like to display Available Pages in Information Page of Config Portal
#define USE_AVAILABLE_PAGES     true
// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
#define USE_ESP_WIFIMANAGER_NTP     false
// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
#define USE_CLOUDFLARE_NTP          false
#define USING_CORS_FEATURE          true
#define USE_CONFIGURABLE_DNS      false

#define WIFI_MULTI_1ST_CONNECT_WAITING_MS       0
#define WIFI_MULTI_CONNECT_WAITING_MS           100L

#define WIFICHECK_INTERVAL           1000L
#define LED_INTERVAL                 2000L
#define HEARTBEAT_INTERVAL          10000L
#define IGRILL_HEARTBEAT_INTERVAL   60000L
