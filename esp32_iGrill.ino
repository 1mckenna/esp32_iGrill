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
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>       //for MQTT
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#include <BLEDevice.h>

#include "FS.h"
#include <LITTLEFS.h>             // https://github.com/lorol/LITTLEFS
FS* filesystem =      &LITTLEFS;
#define FileFS        LITTLEFS
#define FS_Name       "LittleFS"

#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
#define LED_BUILTIN       2
#define LED_ON            HIGH
#define LED_OFF           LOW

const int BUTTON_PIN  = 27;
const int RED_LED     = 26;
const int BLUE_LED    = 25;

#define ESP_DRD_USE_LITTLEFS    true
#define ESP_DRD_USE_SPIFFS      false
#define ESP_DRD_USE_EEPROM      false

#define DOUBLERESETDETECTOR_DEBUG       false  //Enable or Disable DRD Debugging
#include <ESP_DoubleResetDetector.h>      //https://github.com/khoih-prog/ESP_DoubleResetDetector
#define DRD_TIMEOUT 5 // Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0 // RTC Memory Address for the DoubleResetDetector to use
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
#define MQTT_BASETOPIC           "igrill" //If you are using Home Assistant you should set this to your discovery_prefix (default: homeassistant)


// Labels for custom parameters in WiFi manager
#define MQTT_SERVER_Label             "MQTT_SERVER_Label"
#define MQTT_SERVERPORT_Label         "MQTT_SERVERPORT_Label"
#define MQTT_USERNAME_Label           "MQTT_USERNAME_Label"
#define MQTT_PASSWORD_Label           "MQTT_PASSWORD_Label"
#define MQTT_BASETOPIC_Label           "MQTT_BASETOPIC_Label"


// Variables to save custom parameters to...
#define custom_MQTT_SERVER_LEN       20
#define custom_MQTT_PORT_LEN          5
#define custom_MQTT_USERNAME_LEN     20
#define custom_MQTT_PASSWORD_LEN          40
#define custom_MQTT_BASETOPIC_LEN          40

char custom_MQTT_SERVER[custom_MQTT_SERVER_LEN];
char custom_MQTT_SERVERPORT[custom_MQTT_PORT_LEN];
char custom_MQTT_USERNAME[custom_MQTT_USERNAME_LEN];
char custom_MQTT_PASSWORD[custom_MQTT_PASSWORD_LEN];
char custom_MQTT_BASETOPIC[custom_MQTT_BASETOPIC_LEN];

// SSID and PW for Config Portal
String ssid = "ESP32_iGrillClient";
const char* password = "igrill_client";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false
#define MIN_AP_PASSWORD_SIZE    8
#define SSID_MAX_LEN            32
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

static const uint8_t chalBuf[] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}; //iGrill Authentcation Payload
static BLEUUID GENERIC_ATTRIBUTE("00001801-0000-1000-8000-00805f9b34fb"); //UNUSED
static BLEUUID GENERIC_SERVICE_GUID("00001800-0000-1000-8000-00805f9b34fb"); //UNUSED

static BLEUUID BATTERY_SERVICE_UUID("0000180f-0000-1000-8000-00805f9b34fb"); //iGrill BLE Battery Service
static BLEUUID BATTERY_LEVEL("00002A19-0000-1000-8000-00805F9B34FB"); //iGrill BLE Battery Level Characteristic

static BLEUUID AUTH_SERVICE_UUID("64ac0000-4a4b-4b58-9f37-94d3c52ffdf7"); //iGrill BLE Auth Service
static BLEUUID FIRMWARE_VERSION("64ac0001-4a4b-4b58-9f37-94d3c52ffdf7"); // //iGrill BLE Characteristic used getting device firmware info
static BLEUUID APP_CHALLENGE("64ac0002-4a4b-4b58-9f37-94d3c52ffdf7"); //iGrill BLE Characteristic used for Authentication
static BLEUUID DEVICE_CHALLENGE("64ac0003-4a4b-4b58-9f37-94d3c52ffdf7"); //iGrill BLE Characteristic used for Authentication
static BLEUUID DEVICE_RESPONSE("64ac0004-4a4b-4b58-9f37-94d3c52ffdf7"); //iGrill BLE Characteristic used for Authentication

static BLEUUID SERVICE_UUID("A5C50000-F186-4BD6-97F2-7EBACBA0D708"); //iGrillv2 Service
static BLEUUID PROBE1_TEMPERATURE("06EF0002-2E06-4B79-9E33-FCE2C42805EC"); //iGrill BLE Characteristic for Probe1 Temperature
static BLEUUID PROBE1_THRESHOLD("06ef0003-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe1 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE2_TEMPERATURE("06ef0004-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe2 Temperature
static BLEUUID PROBE2_THRESHOLD("06ef0005-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe2 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE3_TEMPERATURE("06ef0006-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe3 Temperature
static BLEUUID PROBE3_THRESHOLD("06ef0007-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe3 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE4_TEMPERATURE("06ef0008-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe4 Temperature
static BLEUUID PROBE4_THRESHOLD("06ef0009-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe4 Notification Threshhold (NOT IMPLEMENTED)

static bool doConnect = false; //bool for determining if an iGrill Device has been found
static bool connected = false; //bool for determining if we are connected and Authenticated to an iGrill Device
static bool reScan = false; //bool for determining if we need to do a BLE Scan (i.e. iGrill Device is no longer available or iGrill wasnt powered on before we finished initial scan)

static BLEClient*  iGrillClient = nullptr;
static BLERemoteCharacteristic* authRemoteCharacteristic = nullptr;
static BLERemoteCharacteristic* batteryCharacteristic = nullptr;
static BLERemoteCharacteristic* probe1TempCharacteristic = nullptr;
static BLERemoteCharacteristic* probe2TempCharacteristic = nullptr;
static BLERemoteCharacteristic* probe3TempCharacteristic = nullptr;
static BLERemoteCharacteristic* probe4TempCharacteristic = nullptr;

static BLERemoteService* iGrillAuthService = nullptr; //iGrill BLE Service used for authenticating to the device (Needed to read probe values)
static BLERemoteService* iGrillService = nullptr; //iGrill BLE Service used for reading probes
static BLERemoteService* iGrillBattService = nullptr; //iGrill BLE Service used to read battery level

static BLEAdvertisedDevice* myDevice;

//The registered iGrill Characteristics call this function when their values change
//We then determine the BLEUUID to figure out how to correctly parse the info then print it to Serial and send to an MQTT server
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData,  size_t length,  bool isNotify)
{
  if(PROBE1_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      // Serial.printf(" ! Probe 1 Disconnected!\n");
      publishProbeTemp(1,-1);
    }
    else
    {
      // Serial.printf(" * Probe 1 Temp: %d\n",pData[0]);
      publishProbeTemp(1,pData[0]);
    }

  }
  else if(PROBE2_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      // Serial.printf(" ! Probe 2 Disconnected!\n");
      publishProbeTemp(2,-1);
    }
    else
    {
      // Serial.printf(" * Probe 2 Temp: %d\n",pData[0]);
      publishProbeTemp(2,pData[0]);
    }

  }
  else if(PROBE3_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      // Serial.printf(" ! Probe 3 Disconnected!\n");
      publishProbeTemp(3,-1);
    }
    else
    {
      // Serial.printf(" * Probe 3 Temp: %d\n",pData[0]);
      publishProbeTemp(3,pData[0]);
    }

  }
  else if(PROBE4_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      // Serial.printf(" ! Probe 4 Disconnected!\n");
      publishProbeTemp(4,-1);
    }
    else
    {
      // Serial.printf(" * Probe 4 Temp: %d\n",pData[0]);
      publishProbeTemp(4,pData[0]);
    }

  }
  else if(BATTERY_LEVEL.equals(pBLERemoteCharacteristic->getUUID()))
  {
    // Serial.printf(" %% Battery Level: %d%%\n",pData[0]);
    publishBattery(pData[0]);
  }
}

class MyClientCallback : public BLEClientCallbacks 
{
  void onConnect(BLEClient* pclient) 
  {
  }

  void onDisconnect(BLEClient* pclient)
  {
    Serial.printf(" - iGrill Disconnected!\n");
    Serial.printf(" - Freeing Memory....\n");
    //Lost Connection to Device Resetting all Variables....
    connected = false; //No longer connected to iGrill
    //Free up memory
    free(iGrillClient);
    free(authRemoteCharacteristic);
    free(batteryCharacteristic);
    free(probe1TempCharacteristic);
    free(probe2TempCharacteristic);
    free(probe3TempCharacteristic);
    free(probe4TempCharacteristic);
    free(iGrillAuthService);
    free(iGrillService);
    free(iGrillBattService);
    Serial.printf(" - Done!\n");
    reScan = true; //Set the BLE rescan flag to true to initiate a new scan
  }
};

//Register Callback for iGrill Probes
void setupProbes()
{
    Serial.println(" - Setting up Probes...");
    if (iGrillService == nullptr) 
    {
      Serial.printf(" - Setting up Probes Failed!\n");
      iGrillClient->disconnect();
    }
    else
    {
      try
      {
        probe1TempCharacteristic = iGrillService->getCharacteristic(PROBE1_TEMPERATURE);
        if(probe1TempCharacteristic->canNotify())
        {
          probe1TempCharacteristic->registerForNotify(notifyCallback);
          Serial.println("  -- Probe 1 Setup!");
        }
        probe2TempCharacteristic = iGrillService->getCharacteristic(PROBE2_TEMPERATURE);
        if(probe2TempCharacteristic->canNotify())
        {
          probe2TempCharacteristic->registerForNotify(notifyCallback);
          Serial.println("  -- Probe 2 Setup!");
        }
        probe3TempCharacteristic = iGrillService->getCharacteristic(PROBE3_TEMPERATURE);
        if(probe3TempCharacteristic->canNotify())
        {
          probe3TempCharacteristic->registerForNotify(notifyCallback);
          Serial.println("  -- Probe 3 Setup!");
        }
        probe4TempCharacteristic = iGrillService->getCharacteristic(PROBE4_TEMPERATURE);
        if(probe4TempCharacteristic->canNotify())
        {
          probe4TempCharacteristic->registerForNotify(notifyCallback);
          Serial.println("  -- Probe 4 Setup!");
        }
      }
      catch(...)
      {
        Serial.printf(" - Setting up Probes Failed!\n");
        iGrillClient->disconnect();
      }
    }
}

//Read and Publish iGrill System Info
void publishSystemInfo()
{
  try
  {
    std::string fwVersion = iGrillAuthService->getCharacteristic(FIRMWARE_VERSION)->readValue();
    // Serial.printf(" - iGrill Firmware Version: %s\n", fwVersion.c_str());
    publishSystemInfo(fwVersion.c_str(), myDevice->getAddress().toString().c_str(), myDevice->getRSSI());
  }
  catch(...)
  {
    Serial.printf("Error obtaining Firmware Info\n");
    iGrillClient->disconnect();
  }
}

//Register Callback for iGrill Device Battery Level
bool setupBatteryCharacteristic()
{
  Serial.println(" - Setting up Battery Characteristic...");
  try
  {
    batteryCharacteristic = iGrillBattService->getCharacteristic(BATTERY_LEVEL);
    if (batteryCharacteristic == nullptr)
    {
      Serial.printf(" - Setting up Battery Characteristic Failed!\n");
      iGrillClient->disconnect();
      return false;
    }
    batteryCharacteristic->registerForNotify(notifyCallback);
    return true;
  }
  catch(...)
  {
    Serial.printf(" - Setting up Battery Characteristic Failed!\n");
    iGrillClient->disconnect();
    return false;
  }
}

//BLE Security Callbacks used for pairing
class MySecurity : public BLESecurityCallbacks 
{
	uint32_t onPassKeyRequest()
	{
		return 000000;
	}
	void onPassKeyNotify(uint32_t pass_key)
	{
    Serial.printf(" - The passkey Notify number:%d\n", pass_key);
	}
	bool onConfirmPIN(uint32_t pass_key)
	{
    Serial.printf(" - The passkey YES/NO number:%d\n", pass_key);
	  vTaskDelay(5000);
		return true;
	}
	bool onSecurityRequest()
	{
		Serial.printf(" - Security Request\n");
		return true;
	}
	void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl)
	{
    Serial.printf(" - iGrill Pair Status: %s\n", auth_cmpl.success ? "Paired" : "Disconnected");
	}
};

/*This function does the following
 - Connects and Pairs to the iGrill Device
 - Sets up the iGrill Authentication Service
       1. Write a challenge (in this case 16 bytes of 0) to APP_CHALLENGE Characteristic
       2. Read DEVICE_CHALLENGE Characteristic Value
       3. Write the DEVICE_CHALLENGE Characteristic Value to DEVICE_RESPONSE Characteristic
          ** Since our first 8 bytes are 0, we dont need to do any crypto operations. We can just hand back the same encrypted value we get and we're authenticated.**
 - Sets up iGrillService (Needed to read probe temps)
 - Sets up iGrillBattService (Needed to read Battery Level)
*/
bool connectToServer() 
{
    Serial.printf("Connecting to iGrill (%s)\n",myDevice->getAddress().toString().c_str());
    //Setting up the iGrill Pairing Paramaters (Without Bonding you can't read the Temp Probes)
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
	  BLEDevice::setSecurityCallbacks(new MySecurity());
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    //End of the iGrill Pairing Paramaters

    iGrillClient  = BLEDevice::createClient(); //Creating the iGrill Client
    if(iGrillClient == nullptr)
    {
      free(iGrillClient);
      reScan = true;
      Serial.printf("Connection Failed!\n");
      return false;
    }
    delay(1*1000);
    if(!iGrillClient->connect(myDevice)) //Connecting to the iGrill Device
    {
      free(iGrillClient);
      reScan = true;
      Serial.printf("Connection Failed!\n");
      return false;
    }
    Serial.println(" - Created client");
    iGrillClient->setClientCallbacks(new MyClientCallback());
    // Connect to the remote BLE Server.
    Serial.println(" - Connected to iGrill BLE Server");
    delay(1*1000);

    // Obtain a reference to the authentication service we are after in the remote BLE server.
    Serial.println(" - Performing iGrill App Authentication");
    iGrillAuthService = iGrillClient->getService(AUTH_SERVICE_UUID);
    delay(1*1000);
    if (iGrillAuthService == nullptr) 
    {
      Serial.printf(" - Authentication Failed (iGrillAuthService is null)\n");
      iGrillClient->disconnect();
      return false;
    }
    delay(2*1000);

    if(iGrillClient->isConnected())
    {
      try
      {
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        authRemoteCharacteristic = iGrillAuthService->getCharacteristic(APP_CHALLENGE);
        if (authRemoteCharacteristic == nullptr) 
        {
          Serial.printf(" - Authentication Failed (authRemoteCharacteristic is null)!\n");
        }
        //Start of Authentication Sequence
        Serial.printf(" - Writing iGrill App Challenge...\n");
        authRemoteCharacteristic->writeValue((uint8_t*)chalBuf, sizeof(chalBuf), true);
        delay(500);
        Serial.printf(" - Reading iGrill Device Challenge\n");
        std::string encrypted_device_challenge = iGrillAuthService->getCharacteristic(DEVICE_CHALLENGE)->readValue();
        delay(500);
        Serial.printf(" - Writing Encrypted iGrill Device Challenge...\n");
        iGrillAuthService->getCharacteristic(DEVICE_RESPONSE)->writeValue(encrypted_device_challenge, true);
        //End of Authentication Sequence
        Serial.printf(" - Authentication Complete\n");
        iGrillService = iGrillClient->getService(SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
        delay(1*1000);
        iGrillBattService = iGrillClient->getService(BATTERY_SERVICE_UUID); //Obtain a reference for the iGrill Battery Service that we use for Getting the Battery Level
        delay(1*1000);
        connected = true;
        reScan = false;
        return true;
      }
      catch(...)
      {
        Serial.printf(" - Authentication Failed!\n");
        iGrillClient->disconnect();
        return false;
      }
    }
    else
    {
      Serial.printf(" - Authentication Failed (lost connection to device)!\n");
      iGrillClient->disconnect();
      return false;
    }
}

//Scan for BLE servers and find the first one that advertises the service we are looking for.
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks 
{
  //This Callback is called for each advertising BLE server.
  void onResult(BLEAdvertisedDevice advertisedDevice) 
  {
    // We have found a device, let us now see if it contains the iGrill service
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(SERVICE_UUID)) 
    {
      Serial.printf("iGrill Device Discovered (%s %d db)\n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI());
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      reScan = true;
    }
  }
};

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

bool publishSystemInfo(const char * fwVersion, const char * iGrillBLEAddress, int iGrillRSSI)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      // Serial.printf("Publishing iGrill Client Info to MQTT\n");
      String topic = (String)MQTT_BASETOPIC + "/igrill/systeminfo";
      String payload = "Network: "+WiFi.SSID()+"\nSignal Strength: "+String(WiFi.RSSI())+"\nIP Address: " + WiFi.localIP().toString() +"\niGrill Device: " + iGrillBLEAddress + "\niGrill Firmware Version: " + fwVersion + "\niGrill Signal Strength: "+String(iGrillRSSI);
      mqtt_client->publish(topic.c_str(),payload.c_str());
    }
  }
  else
  {
    Serial.printf("\nInitiating connection to MQTT\n");
    connectMQTT();
  }
}

void publishProbeTemp(int probeNum, int temp)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      // Serial.printf("Publishing Probe Temp to MQTT\n");
      String topic = (String)MQTT_BASETOPIC + "/igrill/probe_" + String(probeNum);
      mqtt_client->publish(topic.c_str(),String(temp).c_str());
    }
  }
  else
  {
    Serial.printf("\nInitiating connection to MQTT\n");
    connectMQTT();
  }
}

void publishBattery(int battPercent)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      // Serial.printf("Publishing Battery Level to MQTT\n");
      String topic = (String)MQTT_BASETOPIC + "/igrill/battery_level";
      mqtt_client->publish(topic.c_str(),String(battPercent).c_str());
    }
  }
  else
  {
    Serial.printf("\nInitiating connection to MQTT\n");
    connectMQTT();
  }
}

void heartBeatPrint()
{
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("W");        // W means connected to WiFi
  else
    Serial.printf("N");        // N means not connected to WiFi

  if (num == 40)
  {
    Serial.printf("\n");
    num = 1;
  }
  else if (num++ % 5 == 0)
  {
    Serial.printf(" ");
  }
}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    Serial.printf("\nWiFi lost. Call connectMultiWiFi in loop\n");
    delete(mqtt_client);
    delete(client);
    client=NULL;
    mqtt_client=NULL;
    connectMultiWiFi();
  }
}

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong LEDstatus_timeout    = 0;
  static ulong checkwifi_timeout    = 0;
  static ulong igrillheartbeat_timeout = 0;
  
  ulong current_millis = millis();
  #define WIFICHECK_INTERVAL           1000L
  #define LED_INTERVAL                 2000L
  #define HEARTBEAT_INTERVAL          10000L
  #define IGRILL_HEARTBEAT_INTERVAL  300000L

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

  // Print iGrill System Info every IGRILL_HEARTBEAT_INTERVAL (5) minutes.
  if ((current_millis > igrillheartbeat_timeout) || (igrillheartbeat_timeout == 0))
  { 
    publishSystemInfo();
    igrillheartbeat_timeout = current_millis + IGRILL_HEARTBEAT_INTERVAL;
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
  Serial.printf("Button clicked!\n");
  wifi_manager();
}

static void handleDoubleClick() 
{
  Serial.printf("Button double clicked!\n");
}

static void handleLongPressStop() 
{
  Serial.printf("Button pressed for long time and then released!\n");
  newConfigData();
}

void wifi_manager()
{
  Serial.printf("\nConfig Portal requested.\n");
  digitalWrite(LED_BUILTIN, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
  //Local intialization. Once its business is done, there is no need to keep it around
  ESP_WiFiManager ESP_wifiManager("ESP32_iGrillClient");
  //Check if there is stored WiFi router/password credentials.
  //If not found, device will remain in configuration mode until switched off via webserver.
  Serial.printf("Opening Configuration Portal. \n");
  Router_SSID = ESP_wifiManager.WiFi_SSID();
  Router_Pass = ESP_wifiManager.WiFi_Pass();
  if ( !initialConfig && (Router_SSID != "") && (Router_Pass != "") )
  {
    //If valid AP credential and not DRD, set timeout 120s.
    ESP_wifiManager.setConfigPortalTimeout(120);
    Serial.printf("Got stored Credentials. Timeout 120s\n");
  }
  else
  {
    ESP_wifiManager.setConfigPortalTimeout(0);
    Serial.printf("No timeout : ");
    
    if (initialConfig)
    {
      Serial.printf("DRD or No stored Credentials..\n");
    }
    else
    {
      Serial.printf("No stored Credentials.\n");
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
  ESP_WMParameter MQTT_BASETOPIC_FIELD(MQTT_BASETOPIC_Label, "MQTT BASE TOPIC", custom_MQTT_BASETOPIC, custom_MQTT_BASETOPIC_LEN);// MQTT_BASETOPIC
  ESP_wifiManager.addParameter(&MQTT_SERVER_FIELD);
  ESP_wifiManager.addParameter(&MQTT_SERVERPORT_FIELD);
  ESP_wifiManager.addParameter(&MQTT_USERNAME_FIELD);
  ESP_wifiManager.addParameter(&MQTT_PASSWORD_FIELD);
  ESP_wifiManager.addParameter(&MQTT_BASETOPIC_FIELD);
  ESP_wifiManager.setMinimumSignalQuality(-1);
  ESP_wifiManager.setConfigPortalChannel(0);  // Set config portal channel, default = 1. Use 0 => random channel from 1-13
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
    Serial.printf("Not connected to WiFi but continuing anyway.\n");
  }
  else
  {
    Serial.printf("WiFi Connected!\n");
  }
  // Only clear then save data if CP entered and with new valid Credentials
  // No CP => stored getSSID() = ""
  if ( String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getSSID(1)) != "" )
  {
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
  strcpy(custom_MQTT_BASETOPIC, MQTT_BASETOPIC_FIELD.getValue());
  writeConfigFile();  // Writing JSON config file to flash for next boot
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn LED off as we are not in configuration mode.
}

bool readConfigFile() 
{
  File f = FileFS.open(CONFIG_FILE, "r");// this opens the config file in read-mode
  if (!f)
  {
    Serial.printf("Config File not found\n");
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
      Serial.printf("JSON parseObject() failed\n");
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

    if (json.containsKey(MQTT_BASETOPIC_Label))
    {
      strcpy(custom_MQTT_BASETOPIC, json[MQTT_BASETOPIC_Label]);
    }
  }
  Serial.printf("\nConfig File successfully parsed\n");
  return true;
}

bool writeConfigFile() 
{
  Serial.printf("Saving Config File\n");
  DynamicJsonDocument json(1024);
  // JSONify local configuration parameters
  json[MQTT_SERVER_Label]      = custom_MQTT_SERVER;
  json[MQTT_SERVERPORT_Label]  = custom_MQTT_SERVERPORT;
  json[MQTT_USERNAME_Label]    = custom_MQTT_USERNAME;
  json[MQTT_PASSWORD_Label]         = custom_MQTT_PASSWORD;
  json[MQTT_BASETOPIC_Label]         = custom_MQTT_BASETOPIC;

  // Open file for writing
  File f = FileFS.open(CONFIG_FILE, "w");
  if (!f)
  {
    Serial.printf("Failed to open Config File for writing\n");
    return false;
  }

  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);
  f.close();
  Serial.printf("\nConfig File successfully saved\n");
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
  Serial.printf("custom_MQTT_BASETOPIC: %s\n", custom_MQTT_BASETOPIC); 
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
  Serial.printf("MQTT BASETOPIC: %s\n", custom_MQTT_BASETOPIC);
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
  Serial.begin(115200);
  while (!Serial);
  delay(200);
  Serial.printf("\nStarting iGrill BLE Client using %s on %s",String(FS_Name),String(ARDUINO_BOARD));
  Serial.printf("%s\n%s\n",ESP_WIFIMANAGER_VERSION, ESP_DOUBLE_RESET_DETECTOR_VERSION);
  // Initialize the LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  // Format FileFS if not yet
  if (!FileFS.begin(true))
  {
    Serial.printf("%s failed! AutoFormatting.\n"),FS_Name;
  }

  if (!readConfigFile())
  {
    Serial.printf("Failed to read configuration file, using default values\n");
  }

  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);

  if (!readConfigFile())
  {
    Serial.printf("Can't read Config File, using default values\n");
  }

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (!drd)
  {
    Serial.printf("Can't instantiate. Disable DRD feature\n");
  }
  else if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    Serial.printf("Open Config Portal without Timeout: Double Reset Detected\n");
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
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        initialConfig = false;
      }
    }

    if (initialConfig)
    {
      Serial.printf("Open Config Portal without Timeout: No stored WiFi Credentials\n");
      wifi_manager();
    }
    else if ( WiFi.status() != WL_CONNECTED ) 
    {
      Serial.printf("ConnectMultiWiFi in setup\n");
      connectMultiWiFi();
    }
  }
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn led off as we are not in configuration mode.
  BLEDevice::init("ESP32_iGrill");
  BLEDevice::setPower(ESP_PWR_LVL_P7);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  connectMQTT();
}

// Loop function
void loop()
{
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  if (drd)
    drd->loop();
  // this is just for checking if we are connected to WiFi
  // If the flag "doConnect" is true then we have scanned for and found the iGrill Device
  if (doConnect == true) 
  {
    // Now that we have found our device, lets attempt to connect and authenticate.
    if (connectToServer()) 
    {
      publishSystemInfo(); //Get and Publish to MQTT Firmware Information for iGrill Device
      setupBatteryCharacteristic(); //Setup Callbacks to get iGrill Battery Level
      setupProbes(); //Setup Callbacks to get iGrill Probe Temperatures
    }
    doConnect = false; //Resetting doConnect flag
  }

  // Do something now that we are connected to the iGrill Device
  if (connected)
  {
    //We dont need to read any probes or battery level as thats all handled via BLE notifyCallbacks
  }
  else if(reScan)
  {
    Serial.printf("Scanning for iGrill Devices...\n");
    BLEDevice::getScan()->start(0);
  }
  check_status();
}
