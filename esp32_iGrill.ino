/*iGrill BLE Client
In order to setup the iGrill BLE Client or force the device back into configuration mode to update WiFi or MQTT settings you will need to 
press the reset button on the device twice to trigger the double reset detector.

You can configure the Number of seconds to wait for the second reset by changing the  DRD_TIMEOUT variable (Default 10s).

Once the device has booted up in configuration mode you will need to enter a password to enter the configuration/setup panel. 
From this configuration panel you can configure the device to connect to your home WiFi network as well as configure the MQTT Server Information.
NOTE: You can enter in information to connect to two wifi networks, and the device will automatically choose the one with the best signal.
      If you only have a single network you want to use this device from you can leave the second SSID and Password field empty.

The Credentials will then be saved into LittleFS / SPIFFS file and be used to connect to the MQTT Server and publish the iGrill Topics

************************************
* Configuration Portal Information *
************************************
Wifi SSID Name: ESP32_iGrillClient
Wifi Password: igrill_client

NOTE: You can change this from the default by editing the code @ Line XXXX before uploading to the device.
Wifi MQTT handling based on the example provided by the ESP_WifiManager Library for handling Wifi/MQTT Setup (https://github.com/khoih-prog/ESP_WiFiManager)
*/

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _WIFIMGR_LOGLEVEL_    3
#include <Arduino.h>            // for button
#include <FS.h>
#include <ArduinoJson.h>        // get it from https://arduinojson.org/ or install via Arduino library manager
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>       //for MQTT
#include <WiFiMulti.h>
WiFiMulti wifiMulti;

// LittleFS has higher priority than SPIFFS
#define USE_LITTLEFS    true
#define USE_SPIFFS      false

#if USE_LITTLEFS
  // Use LittleFS
  #include "FS.h"
  // The library will be depreciated after being merged to future major Arduino esp32 core release 2.x
  // At that time, just remove this library inclusion
  #include <LITTLEFS.h>             // https://github.com/lorol/LITTLEFS
  FS* filesystem =      &LITTLEFS;
  #define FileFS        LITTLEFS
  #define FS_Name       "LittleFS"
#elif USE_SPIFFS
  #include <SPIFFS.h>
  FS* filesystem =      &SPIFFS;
  #define FileFS        SPIFFS
  #define FS_Name       "SPIFFS"
#else
  // Use FFat
  #include <FFat.h>
  FS* filesystem =      &FFat;
  #define FileFS        FFat
  #define FS_Name       "FFat"
#endif

#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
#define LED_BUILTIN       2
#define LED_ON            HIGH
#define LED_OFF           LOW

const int BUTTON_PIN  = 27;
const int RED_LED     = 26;
const int BLUE_LED    = 25;


#if USE_LITTLEFS
  #define ESP_DRD_USE_LITTLEFS    true
  #define ESP_DRD_USE_SPIFFS      false
  #define ESP_DRD_USE_EEPROM      false
#elif USE_SPIFFS
  #define ESP_DRD_USE_LITTLEFS    false
  #define ESP_DRD_USE_SPIFFS      true
  #define ESP_DRD_USE_EEPROM      false
#else
 #define ESP_DRD_USE_LITTLEFS    false
  #define ESP_DRD_USE_SPIFFS      false
  #define ESP_DRD_USE_EEPROM      true
#endif
#define DOUBLERESETDETECTOR_DEBUG       true  //false
#include <ESP_DoubleResetDetector.h>      //https://github.com/khoih-prog/ESP_DoubleResetDetector
// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10
// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0
DoubleResetDetector* drd = NULL;

uint32_t timer = millis();
const char* CONFIG_FILE = "/ConfigMQTT.json";

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false; //default false

// Default configuration values for MQTT
#define MQTT_SERVER              "127.0.0.1"
#define MQTT_SERVERPORT          "1883"
#define MQTT_USERNAME            "mqtt"
#define MQTT_PASSWORD            "password"

// Labels for custom parameters in WiFi manager
#define MQTT_SERVER_Label             "MQTT_SERVER_Label"
#define MQTT_SERVERPORT_Label         "MQTT_SERVERPORT_Label"
#define MQTT_USERNAME_Label           "MQTT_USERNAME_Label"
#define MQTT_PASSWORD_Label           "MQTT_PASSWORD_Label"

// Variables to save custom parameters to...
// I would like to use these instead of #defines
#define custom_MQTT_SERVER_LEN       20
#define custom_MQTT_PORT_LEN          5
#define custom_MQTT_USERNAME_LEN     20
#define custom_MQTT_PASSWORD_LEN          40

char custom_MQTT_SERVER[custom_MQTT_SERVER_LEN];
char custom_MQTT_SERVERPORT[custom_MQTT_PORT_LEN];
char custom_MQTT_USERNAME[custom_MQTT_USERNAME_LEN];
char custom_MQTT_PASSWORD[custom_MQTT_PASSWORD_LEN];

// For Config Portal
// SSID and PW for Config Portal
String ssid = "ESP32_iGrillClient";
const char* password = "igrill_client";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// From v1.1.0
// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false

#define MIN_AP_PASSWORD_SIZE    8

#define SSID_MAX_LEN            32
//From v1.0.10, WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN            64

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS      2

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
} WM_Config;

WM_Config         WM_config;

#define  CONFIG_FILENAME              F("/wifi_cred.dat")

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESP_WiFiManager.h>
#define USE_AVAILABLE_PAGES     true

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
//#define USE_STATIC_IP_CONFIG_IN_CP          false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP     false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false
#define USING_CORS_FEATURE          true

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
// Force DHCP to be true
#if defined(USE_DHCP_IP)
#undef USE_DHCP_IP
#endif
#define USE_DHCP_IP     true
#else
// You can select DHCP or Static IP here
#define USE_DHCP_IP     true
//#define USE_DHCP_IP     false
#endif

#if ( USE_DHCP_IP || ( defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP ) )
  // Use DHCP
  #warning Using DHCP IP
  IPAddress stationIP   = IPAddress(0, 0, 0, 0);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#else
  // Use static IP
  #warning Using static IP
  IPAddress stationIP   = IPAddress(192, 168, 2, 232);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#endif

#define USE_CONFIGURABLE_DNS      false

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

#include <ESP_WiFiManager.h>              //https://github.com/khoih-prog/ESP_WiFiManager


WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

// Create an ESP32 WiFiClient class to connect to the MQTT server
WiFiClient *client = NULL;
PubSubClient *mqtt_client = NULL;


void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
  in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
  in_WM_STA_IPconfig._sta_static_ip   = stationIP;
  in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn   = netMask;
#if USE_CONFIGURABLE_DNS  
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, ", dns2IP =", in_WM_STA_IPconfig._sta_static_dns2);
#endif
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  #if USE_CONFIGURABLE_DNS  
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);  
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
  #endif 
}

///////////////////////////////////////////

uint8_t connectMultiWiFi()
{
  #define WIFI_MULTI_1ST_CONNECT_WAITING_MS       0
  #define WIFI_MULTI_CONNECT_WAITING_MS           100L
  uint8_t status;
  LOGERROR(F("ConnectMultiWiFi with :"));
  if ( (Router_SSID != "") && (Router_Pass != "") )
  {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
  }
  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
  {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
    {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
    }
  }
  LOGERROR(F("Connecting MultiWifi..."));
  WiFi.mode(WIFI_STA);

#if !USE_DHCP_IP
  configWiFi(WM_STA_IPconfig);
#endif

  int i = 0;
  status = wifiMulti.run();
  delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

  while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
  {
    status = wifiMulti.run();

    if ( status == WL_CONNECTED )
      break;
    else
      delay(WIFI_MULTI_CONNECT_WAITING_MS);
  }

  if ( status == WL_CONNECTED )
  {
    LOGERROR1(F("WiFi connected after time: "), i);
    LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
    LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
  }
  else
    LOGERROR(F("WiFi not connected"));

  return status;
}

void toggleLED()
{
  //toggle state
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

bool mqttPublish()
{
  Serial.printf("Entering MQTT Publish\n");
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      Serial.printf("Connected to MQTT\nAttempting to Publish\n");
      mqtt_client->publish("igrill/status","SUCCESS");
    }
  }
  else
  {
    Serial.printf("Initiating connection to MQTT\n");
    connectMQTT();
  }
  
  
}

void heartBeatPrint()
{
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("W"));        // W means connected to WiFi
  else
    Serial.print(F("N"));        // N means not connected to WiFi

  if (num == 40)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 5 == 0)
  {
    Serial.print(F(" "));
  }
}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    Serial.println(F("\nWiFi lost. Call connectMultiWiFi in loop"));
    connectMultiWiFi();
  }
}

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong LEDstatus_timeout    = 0;
  static ulong checkwifi_timeout    = 0;
  static ulong mqtt_publish_timeout = 0;
  
  ulong current_millis = millis();
  #define WIFICHECK_INTERVAL    1000L
  #define LED_INTERVAL          2000L
  #define HEARTBEAT_INTERVAL    10000L
  #define PUBLISH_INTERVAL      60000L

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  if ((current_millis > LEDstatus_timeout) || (LEDstatus_timeout == 0))
  {
    // Toggle LED at LED_INTERVAL = 2s
    toggleLED();
    LEDstatus_timeout = current_millis + LED_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
  { 
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }

  // Check every PUBLISH_INTERVAL (60) seconds.
  if ((current_millis > mqtt_publish_timeout) || (mqtt_publish_timeout == 0))
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      mqttPublish();
    }
    
    mqtt_publish_timeout = current_millis + PUBLISH_INTERVAL;
  }

}

bool loadConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));
  memset(&WM_config,       0, sizeof(WM_config));
  memset(&WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  if (file)
  {
    file.readBytes((char *) &WM_config,   sizeof(WM_config));
    file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    file.close();
    LOGERROR(F("OK"));
    displayIPConfigStruct(WM_STA_IPconfig);
    return true;
  }
  else
  {
    LOGERROR(F("failed"));
    return false;
  }
}
    
void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));

  if (file)
  {
    file.write((uint8_t*) &WM_config,   sizeof(WM_config));
    file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    file.close();
    LOGERROR(F("OK"));
  }
  else
  {
    LOGERROR(F("failed"));
  }
}

//event handler functions for button
static void handleClick() 
{
  Serial.println(F("Button clicked!"));
  wifi_manager();
}

static void handleDoubleClick() 
{
  Serial.println(F("Button double clicked!"));
}

static void handleLongPressStop() 
{
  Serial.println(F("Button pressed for long time and then released!"));
  newConfigData();
}

void wifi_manager()
{
  Serial.println(F("\nConfig Portal requested."));
  digitalWrite(LED_BUILTIN, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
  //Local intialization. Once its business is done, there is no need to keep it around
  ESP_WiFiManager ESP_wifiManager("ESP32_iGrillClient");
  //Check if there is stored WiFi router/password credentials.
  //If not found, device will remain in configuration mode until switched off via webserver.
  Serial.print(F("Opening Configuration Portal. "));
  Router_SSID = ESP_wifiManager.WiFi_SSID();
  Router_Pass = ESP_wifiManager.WiFi_Pass();
  if ( !initialConfig && (Router_SSID != "") && (Router_Pass != "") )
  {
    //If valid AP credential and not DRD, set timeout 120s.
    ESP_wifiManager.setConfigPortalTimeout(120);
    Serial.println(F("Got stored Credentials. Timeout 120s"));
  }
  else
  {
    ESP_wifiManager.setConfigPortalTimeout(0);
    Serial.print(F("No timeout : "));
    
    if (initialConfig)
    {
      Serial.println(F("DRD or No stored Credentials.."));
    }
    else
    {
      Serial.println(F("No stored Credentials."));
    }
  }
  //Local intialization. Once its business is done, there is no need to keep it around

  // Extra parameters to be configured
  // After connecting, parameter.getValue() will get you the configured value
  // Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>
  // (*** we are not using <custom HTML> and <label placement> ***)
  ESP_WMParameter MQTT_SERVER_FIELD(MQTT_SERVER_Label, "MQTT SERVER", custom_MQTT_SERVER, custom_MQTT_SERVER_LEN);// MQTT_SERVER
  ESP_WMParameter MQTT_SERVERPORT_FIELD(MQTT_SERVERPORT_Label, "MQTT SERVER PORT", custom_MQTT_SERVERPORT, custom_MQTT_PORT_LEN + 1);// MQTT_SERVERPORT
  ESP_WMParameter MQTT_USERNAME_FIELD(MQTT_USERNAME_Label, "MQTT USERNAME", custom_MQTT_USERNAME, custom_MQTT_USERNAME_LEN);// MQTT_USERNAME
  ESP_WMParameter MQTT_PASSWORD_FIELD(MQTT_PASSWORD_Label, "MQTT PASSWORD", custom_MQTT_PASSWORD, custom_MQTT_PASSWORD_LEN);// MQTT_PASSWORD
  ESP_wifiManager.addParameter(&MQTT_SERVER_FIELD);
  ESP_wifiManager.addParameter(&MQTT_SERVERPORT_FIELD);
  ESP_wifiManager.addParameter(&MQTT_USERNAME_FIELD);
  ESP_wifiManager.addParameter(&MQTT_PASSWORD_FIELD);
  ESP_wifiManager.setMinimumSignalQuality(-1);
  // Set config portal channel, default = 1. Use 0 => random channel from 1-13
  ESP_wifiManager.setConfigPortalChannel(0);
  //set custom ip for portal
  //ESP_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 100, 1), IPAddress(192, 168, 100, 1), IPAddress(255, 255, 255, 0));
#if !USE_DHCP_IP    
  #if USE_CONFIGURABLE_DNS
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask, dns1IP, dns2IP);
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask);
  #endif 
#endif  

  // New from v1.1.1
#if USING_CORS_FEATURE
  ESP_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // Start an access point
  // and goes into a blocking loop awaiting configuration.
  // Once the user leaves the portal with the exit button
  // processing will continue
  if (!ESP_wifiManager.startConfigPortal((const char *) ssid.c_str(), password))
  {
    Serial.println(F("Not connected to WiFi but continuing anyway."));
  }
  else
  {
    // If you get here you have connected to the WiFi
    Serial.println(F("Connected...yeey :)"));
    Serial.print(F("Local IP: "));
    Serial.println(WiFi.localIP());
  }
  // Only clear then save data if CP entered and with new valid Credentials
  // No CP => stored getSSID() = ""
  if ( String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getSSID(1)) != "" )
  {
    // Stored  for later usage, from v1.1.0, but clear first
    memset(&WM_config, 0, sizeof(WM_config));
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      String tempSSID = ESP_wifiManager.getSSID(i);
      String tempPW   = ESP_wifiManager.getPW(i);
      if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
      if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);  
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }
    ESP_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
    displayIPConfigStruct(WM_STA_IPconfig);
    saveConfigData();
  }

  // Getting posted form values and overriding local variables parameters
  // Config file is written regardless the connection state
  strcpy(custom_MQTT_SERVER, MQTT_SERVER_FIELD.getValue());
  strcpy(custom_MQTT_SERVERPORT, MQTT_SERVERPORT_FIELD.getValue());
  strcpy(custom_MQTT_USERNAME, MQTT_USERNAME_FIELD.getValue());
  strcpy(custom_MQTT_PASSWORD, MQTT_PASSWORD_FIELD.getValue());
  writeConfigFile();  // Writing JSON config file to flash for next boot
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn LED off as we are not in configuration mode.
}

bool readConfigFile() 
{
  File f = FileFS.open(CONFIG_FILE, "r");// this opens the config file in read-mode
  if (!f)
  {
    Serial.println(F("Config File not found"));
    return false;
  }
  else
  {// we could open the file
    size_t size = f.size();
    
    std::unique_ptr<char[]> buf(new char[size + 1]); // Allocate a buffer to store contents of the file.
    f.readBytes(buf.get(), size); // Read and store file contents in buf
    f.close();
    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());    
    if ( deserializeError )
    {
      Serial.println(F("JSON parseObject() failed"));
      return false;
    }  
    serializeJson(json, Serial);
    // Parse all config file parameters, override
    // local config variables with parsed values
    if (json.containsKey(MQTT_SERVER_Label))
    {
      strcpy(custom_MQTT_SERVER, json[MQTT_SERVER_Label]);
    }

    if (json.containsKey(MQTT_SERVERPORT_Label))
    {
      strcpy(custom_MQTT_SERVERPORT, json[MQTT_SERVERPORT_Label]);
    }

    if (json.containsKey(MQTT_USERNAME_Label))
    {
      strcpy(custom_MQTT_USERNAME, json[MQTT_USERNAME_Label]);
    }

    if (json.containsKey(MQTT_PASSWORD_Label))
    {
      strcpy(custom_MQTT_PASSWORD, json[MQTT_PASSWORD_Label]);
    }
  }
  Serial.println(F("\nConfig File successfully parsed"));
  return true;
}

bool writeConfigFile() 
{
  Serial.println(F("Saving Config File"));
  DynamicJsonDocument json(1024);
  // JSONify local configuration parameters
  json[MQTT_SERVER_Label]      = custom_MQTT_SERVER;
  json[MQTT_SERVERPORT_Label]  = custom_MQTT_SERVERPORT;
  json[MQTT_USERNAME_Label]    = custom_MQTT_USERNAME;
  json[MQTT_PASSWORD_Label]         = custom_MQTT_PASSWORD;

  // Open file for writing
  File f = FileFS.open(CONFIG_FILE, "w");
  if (!f)
  {
    Serial.println(F("Failed to open Config File for writing"));
    return false;
  }

  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);
  f.close();
  Serial.println(F("\nConfig File successfully saved"));
  return true;
}

// this function is just to display newly saved data,
// it is not necessary though, because data is displayed
// after WiFi manager resets ESP32
void newConfigData() 
{
  Serial.printf("\ncustom_MQTT_SERVER: %s\n",custom_MQTT_SERVER); 
  Serial.printf("custom_MQTT_SERVERPORT: %s\n",custom_MQTT_SERVERPORT); 
  Serial.printf("custom_MQTT_USERNAME: %s\n",custom_MQTT_USERNAME); 
  Serial.printf("custom_MQTT_PASSWORD: %s\n", custom_MQTT_PASSWORD); 
}

void connectMQTT() 
{
  Serial.printf("Connecting to MQTT...\n");
  if (!client)
  {
    client = new WiFiClient();
    Serial.printf("\nCreating new WiFi client object : %s\n",client?"OK":"failed");
  }
  if(!mqtt_client)
  {
    mqtt_client = new PubSubClient(*client);
    mqtt_client->setBufferSize(512);
    Serial.printf("\nCreating new MQTT client object : %s\n",mqtt_client?"OK":"failed");
    mqtt_client->setServer(custom_MQTT_SERVER, atoi(custom_MQTT_SERVERPORT));
  }
  Serial.printf("MQTT Server: %s:%d\n", custom_MQTT_SERVER,atoi(custom_MQTT_SERVERPORT));
  Serial.printf("MQTT User: %s\n", custom_MQTT_USERNAME);
  Serial.printf("MQTT Password: %s\n", custom_MQTT_PASSWORD);
  if (!mqtt_client->connect("iGrill", custom_MQTT_USERNAME, custom_MQTT_PASSWORD)) 
  {
    Serial.printf("MQTT connection failed: %d\n",mqtt_client->state());
  }
  else
  {
    Serial.printf("MQTT connected\n\n");
  }
}

// Setup function
void setup()
{
  // Put your setup code here, to run once
  Serial.begin(115200);
  while (!Serial);

  delay(200);

  Serial.print("\nStarting ConfigOnDRD_FS_MQTT_Ptr_Complex using " + String(FS_Name));
  Serial.println(" on " + String(ARDUINO_BOARD));
  Serial.println(ESP_WIFIMANAGER_VERSION);
  Serial.println(ESP_DOUBLE_RESET_DETECTOR_VERSION);


  // Initialize the LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);

  // Format FileFS if not yet
  if (!FileFS.begin(true))
  {
    Serial.print(FS_Name);
    Serial.println(F(" failed! AutoFormatting."));
  }

  if (!readConfigFile())
  {
    Serial.println(F("Failed to read configuration file, using default values"));
  }

  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);

  if (!readConfigFile())
  {
    Serial.println(F("Can't read Config File, using default values"));
  }

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

  if (!drd)
  {
    Serial.println(F("Can't instantiate. Disable DRD feature"));
  }
  else if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    //ESP_wifiManager.setConfigPortalTimeout(0);
    
    Serial.println(F("Open Config Portal without Timeout: Double Reset Detected"));
    initialConfig = true;
  }
 
  if (initialConfig)
  {
    wifi_manager();
  }
  else
  {   
    // Load stored data, the addAP ready for MultiWiFi reconnection
    loadConfigData();

    // Pretend CP is necessary as we have no AP Credentials
    initialConfig = true;

    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        initialConfig = false;
      }
    }

    if (initialConfig)
    {
      Serial.println(F("Open Config Portal without Timeout: No stored WiFi Credentials"));
    
      wifi_manager();
    }
    else if ( WiFi.status() != WL_CONNECTED ) 
    {
      Serial.println(F("ConnectMultiWiFi in setup"));
     
      connectMultiWiFi();
    }
  }
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn led off as we are not in configuration mode.  
  connectMQTT();
}

// Loop function
void loop()
{
  // checking button state all the time
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  if (drd)
    drd->loop();
  // this is just for checking if we are connected to WiFi
  check_status();
}
