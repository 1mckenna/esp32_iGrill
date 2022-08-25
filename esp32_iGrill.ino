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
Wifi SSID Name: iGrillClient_<ESP32_Chip_ID>
Wifi Password: igrill_client

NOTE: You can change this from the default by editing the code @ Line 58 before uploading to the device.
Wifi MQTT handling based on the example provided by the ESP_WifiManager Library for handling Wifi/MQTT Setup (https://github.com/khoih-prog/ESP_WiFiManager)
*/

#include "config.h" //iGrill BLE Client Configurable Settings
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h> //for MQTT
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#include <ESP_WiFiManager.h> //https://github.com/khoih-prog/ESP_WiFiManager
#include <BLEDevice.h>

#include <LittleFS.h>       // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS
FS* filesystem = &LittleFS;

const int BUTTON_PIN  = 27;
const int RED_LED     = 26;
const int BLUE_LED    = 25;

#include <ESP_DoubleResetDetector.h> //https://github.com/khoih-prog/ESP_DoubleResetDetector
DoubleResetDetector* drd = NULL;

uint32_t timer = millis();
const char* CONFIG_FILE = "/ConfigMQTT.json";

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false; //default false

//Setting up variables for MQTT Info in Config Portal
char custom_MQTT_SERVER[custom_MQTT_SERVER_LEN];
char custom_MQTT_SERVERPORT[custom_MQTT_PORT_LEN];
char custom_MQTT_USERNAME[custom_MQTT_USERNAME_LEN];
char custom_MQTT_PASSWORD[custom_MQTT_PASSWORD_LEN];
char custom_MQTT_BASETOPIC[custom_MQTT_BASETOPIC_LEN];
char customhtml[24] = "type=\"checkbox\""; //Used for Metric Degrees checkbox in Config Portal
bool USE_METRIC_DEGREES = false; //Default to Imperial Degrees
char customhtmlNoMultiWifi[24] = "type=\"checkbox\""; //Used for MultiWifi checkbox in Config Portal
bool NO_MULTI_WIFI = false; // Default use MultiWifi


// SSID and PW for Config Portal
String ssid = "iGrillClient_" + String(ESP_getChipId(), HEX);
const char* password = "igrill_client";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

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

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
} WM_Config;

WM_Config         WM_config;

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
  // Force DHCP to be true
  #if defined(USE_DHCP_IP)
    #undef USE_DHCP_IP
  #endif
  #define USE_DHCP_IP     true
#else
  #define USE_DHCP_IP     true
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

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

// Create an ESP32 WiFiClient class to connect to the MQTT server
WiFiClient *client = nullptr;
PubSubClient *mqtt_client = nullptr;

//iGrill BLE Client Logging Function
void IGRILLLOGGER(String logMsg, int requiredLVL)
{
  if(requiredLVL <= IGRILL_DEBUG_LVL)
    Serial.printf("[II] %s\n", logMsg.c_str());
}

#pragma region iGrill_BLE_Variables
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

static BLEUUID MINI_SERVICE_UUID("63C70000-4A82-4261-95FF-92CF32477861"); //iGrill mini Service
static BLEUUID MINIV2_SERVICE_UUID("9d610c43-ae1d-41a9-9b09-3c7ecd5c6035"); //iGrill mini v2 Service
static BLEUUID SERVICE_UUID("A5C50000-F186-4BD6-97F2-7EBACBA0D708"); //iGrillv2 Service
static BLEUUID V202_SERVICE_UUID("ADA7590F-2E6D-469E-8F7B-1822B386A5E9"); //iGrillv202 Service
static BLEUUID V3_SERVICE_UUID("6E910000-58DC-41C7-943F-518B278CEA88"); //iGrillv3 Service

static BLEUUID PROBE1_TEMPERATURE("06EF0002-2E06-4B79-9E33-FCE2C42805EC"); //iGrill BLE Characteristic for Probe1 Temperature
static BLEUUID PROBE1_THRESHOLD("06ef0003-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe1 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE2_TEMPERATURE("06ef0004-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe2 Temperature
static BLEUUID PROBE2_THRESHOLD("06ef0005-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe2 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE3_TEMPERATURE("06ef0006-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe3 Temperature
static BLEUUID PROBE3_THRESHOLD("06ef0007-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe3 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROBE4_TEMPERATURE("06ef0008-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe4 Temperature
static BLEUUID PROBE4_THRESHOLD("06ef0009-2e06-4b79-9e33-fce2c42805ec"); //iGrill BLE Characteristic for Probe4 Notification Threshhold (NOT IMPLEMENTED)
static BLEUUID PROPANE_SERVICE("f5d40000-3548-4c22-9947-f3673fce3cd9");//iGrill v3 Propane Service
static BLEUUID PROPANE_LEVEL_SENSOR("f5d40001-3548-4c22-9947-f3673fce3cd9"); //iGrill v3 Propane sensor

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
static BLERemoteCharacteristic* propanelevelCharacteristic = nullptr;

static BLERemoteService* iGrillAuthService = nullptr; //iGrill BLE Service used for authenticating to the device (Needed to read probe values)
static BLERemoteService* iGrillService = nullptr; //iGrill BLE Service used for reading probes
static BLERemoteService* iGrillBattService = nullptr; //iGrill BLE Service used to read battery level
static BLERemoteService* iGrillPropaneService = nullptr; //iGrill BLE Service used to read propane level from iGrillv3 devices

static BLEAdvertisedDevice* myDevice;
static String deviceStr ="";
static String iGrillMac="";
static String iGrillModel="";
static bool has_propane_sensor=false;

#define DELETE(ptr) { if (ptr != nullptr) {delete ptr; ptr = nullptr;} }
#pragma endregion
#pragma region iGrill_BLE_Functions
//The registered iGrill Characteristics call this function when their values change
//We then determine the BLEUUID to figure out how to correctly parse the info then print it to Serial and send to an MQTT server
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData,  size_t length,  bool isNotify)
{
  if(PROBE1_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      IGRILLLOGGER(" ! Probe 1 Disconnected!", 2);
      publishProbeTemp(1,-100); //We use -100 as the lowest supported temp via the probes is -58
    }
    else
    {
      short t = (pData[1] << 8) | pData[0];
      if (USE_METRIC_DEGREES)
        t=(t-32)/1.8; // if metrical, convert to celsius
      IGRILLLOGGER(" * Probe 1 Temp: " + String(t), 2);
      publishProbeTemp(1, t);
    }
  }
  else if(PROBE2_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      IGRILLLOGGER(" ! Probe 2 Disconnected!",2);
      publishProbeTemp(2,-100); //We use -100 as the lowest supported temp via the probes is -58
    }
    else
    {
      short t = (pData[1] << 8) | pData[0];
      if (USE_METRIC_DEGREES)
        t=(t-32)/1.8; // if metrical, convert to celsius
      IGRILLLOGGER(" * Probe 2 Temp: " + String(t), 2);
      publishProbeTemp(2, t);
    }
  }
  else if(PROBE3_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      IGRILLLOGGER(" ! Probe 3 Disconnected!",2);
      publishProbeTemp(3,-100); //We use -100 as the lowest supported temp via the probes is -58
    }
    else
    {
      short t = (pData[1] << 8) | pData[0];
      if (USE_METRIC_DEGREES)
        t=(t-32)/1.8; // if metrical, convert to celsius
      IGRILLLOGGER(" * Probe 3 Temp: " + String(t), 2);
      publishProbeTemp(3, t);
    }
  }
  else if(PROBE4_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      IGRILLLOGGER(" ! Probe 4 Disconnected!",2);
      publishProbeTemp(4,-100); //We use -100 as the lowest supported temp via the probes is -58
    }
    else
    {
      short t = (pData[1] << 8) | pData[0];
      if (USE_METRIC_DEGREES)
        t=(t-32)/1.8; // if metrical, convert to celsius
      IGRILLLOGGER(" * Probe 4 Temp: " + String(t), 2);
      publishProbeTemp(4, t);
    }
  }
  else if(BATTERY_LEVEL.equals(pBLERemoteCharacteristic->getUUID()))
  {
    IGRILLLOGGER(" %% Battery Level: " + String(pData[0]) + "%% ",2);
    publishBattery(pData[0]);
  }
  else if(PROPANE_LEVEL_SENSOR.equals(pBLERemoteCharacteristic->getUUID()))
  {
    //WE RECIEVED A NEW PROPANE LEVEL EVENT
    IGRILLLOGGER(" %% Propane Level: " + String(pData[0]*25) + "%% ",2);
    publishPropaneLevel(pData[0]*25);
  }

}

class MyClientCallback : public BLEClientCallbacks 
{
  void onConnect(BLEClient* pclient) 
  {
  }

  void onDisconnect(BLEClient* pclient)
  {
    IGRILLLOGGER(" - iGrill Disconnected!", 1);
    IGRILLLOGGER(" - Freeing Memory....", 1);
    //Lost Connection to Device Resetting all Variables....
    connected = false; //No longer connected to iGrill
    //Free up memory
    DELETE(iGrillClient);

    // the service references are owned by iGrillClient and are deleted by it's destructor
    authRemoteCharacteristic = nullptr;
    batteryCharacteristic = nullptr;
    probe1TempCharacteristic = nullptr;
    probe2TempCharacteristic = nullptr;
    probe3TempCharacteristic = nullptr;
    probe4TempCharacteristic = nullptr;
    propanelevelCharacteristic = nullptr;
    iGrillAuthService = nullptr;
    iGrillService = nullptr;
    iGrillBattService = nullptr;
    iGrillPropaneService = nullptr;
    has_propane_sensor=false;
    deviceStr =""; //Reset the Device String used for MQTT publishing
    iGrillMac=""; //Reset the iGrillMac String used for MQTT publishing
    iGrillModel=""; //Reset the iGrillModel String used for MQTT publishing
    disconnectMQTT();
    IGRILLLOGGER(" - Done!", 1);
    reScan = true; //Set the BLE rescan flag to true to initiate a new scan
  }
};

static MyClientCallback oMyClientCallback;

//Register Callback for iGrill Probes
void setupProbes()
{
    IGRILLLOGGER(" - Setting up Probes...",1);
    if (iGrillService == nullptr) 
    {
      IGRILLLOGGER(" - Setting up Probes Failed!",1);
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
          IGRILLLOGGER("  -- Probe 1 Setup!",1);
        }
        if((iGrillModel != "iGrill_mini") && (iGrillModel != "iGrill_mini_v2"))
        {
          probe2TempCharacteristic = iGrillService->getCharacteristic(PROBE2_TEMPERATURE);
          if(probe2TempCharacteristic->canNotify())
          {
            probe2TempCharacteristic->registerForNotify(notifyCallback);
            IGRILLLOGGER("  -- Probe 2 Setup!",1);
          }
          probe3TempCharacteristic = iGrillService->getCharacteristic(PROBE3_TEMPERATURE);
          if(probe3TempCharacteristic->canNotify())
          {
           probe3TempCharacteristic->registerForNotify(notifyCallback);
           IGRILLLOGGER("  -- Probe 3 Setup!",1);
          }
          probe4TempCharacteristic = iGrillService->getCharacteristic(PROBE4_TEMPERATURE);
          if(probe4TempCharacteristic->canNotify())
          {
            probe4TempCharacteristic->registerForNotify(notifyCallback);
            IGRILLLOGGER("  -- Probe 4 Setup!",1);
          }
        }
      }
      catch(...)
      {
        IGRILLLOGGER(" - Setting up Probes Failed!",1);
        iGrillClient->disconnect();
      }
    }
}

//Read and Publish iGrill System Info
void getiGrillInfo()
{
  try
  {
    std::string fwVersion = iGrillAuthService->getCharacteristic(FIRMWARE_VERSION)->readValue();
    if(deviceStr == "")
      setDeviceJSONObject(fwVersion.c_str(), myDevice->getAddress().toString().c_str());
    publishSystemInfo();
  }
  catch(...)
  {
    IGRILLLOGGER("Error obtaining Firmware Info", 1);
    iGrillClient->disconnect();
  }
}

//Register Callback for iGrill Device Propane Level
bool setupPropaneCharacteristic()
{
  IGRILLLOGGER(" - Setting up Propane Level Characteristic...",1);
  try
  {
    propanelevelCharacteristic = iGrillPropaneService->getCharacteristic(PROPANE_LEVEL_SENSOR);
    if (propanelevelCharacteristic == nullptr)
    {
      IGRILLLOGGER(" - Setting up Propane Level Characteristic Failed!", 1);
      iGrillClient->disconnect();
      return false;
    }
    if (propanelevelCharacteristic->canNotify())
    {
      propanelevelCharacteristic->registerForNotify(notifyCallback);
      IGRILLLOGGER("  -- Propane Level Setup!",1);
    }
    if (propanelevelCharacteristic->canRead())
    {
      uint8_t value = propanelevelCharacteristic->readUInt8();
      IGRILLLOGGER(" %% Propane Level: " + String(value*25) + "%", 2);
      publishPropaneLevel(value*25);
    }
    return true;
  }
  catch(...)
  {
    IGRILLLOGGER(" - Setting up Propane Level Characteristic Failed!", 1);
    iGrillClient->disconnect();
    return false;
  }

}


//Register Callback for iGrill Device Battery Level
bool setupBatteryCharacteristic()
{
  IGRILLLOGGER(" - Setting up Battery Characteristic...",1);
  try
  {
    batteryCharacteristic = iGrillBattService->getCharacteristic(BATTERY_LEVEL);
    if (batteryCharacteristic == nullptr)
    {
      IGRILLLOGGER(" - Setting up Battery Characteristic Failed!", 1);
      iGrillClient->disconnect();
      return false;
    }
    if (batteryCharacteristic->canNotify())
    {
      batteryCharacteristic->registerForNotify(notifyCallback);
      IGRILLLOGGER("  -- Battery Setup!",1);
    }
    if (batteryCharacteristic->canRead())
    {
      uint8_t value = batteryCharacteristic->readUInt8();
      IGRILLLOGGER(" %% Battery Level: " + String(value) + "%", 2);
      publishBattery(value);
    }
    return true;
  }
  catch(...)
  {
    IGRILLLOGGER(" - Setting up Battery Characteristic Failed!", 1);
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
    IGRILLLOGGER(" - The passkey Notify number: " + String(pass_key),0);
	}
	bool onConfirmPIN(uint32_t pass_key)
	{
    IGRILLLOGGER(" - The passkey YES/NO number: " + String(pass_key), 0);
	  vTaskDelay(5000);
		return true;
	}
	bool onSecurityRequest()
	{
		IGRILLLOGGER(" - Security Request", 0);
		return true;
	}
	void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl)
	{
    Serial.printf("[II]  - iGrill Pair Status: %s\n", auth_cmpl.success ? "Paired" : "Disconnected");
	}
};

static MySecurity oMySecurity;

/*This function does the following
 - Connects and Pairs to the iGrill Device
 - Sets up the iGrill Authentication Service
       1. Write a challenge (in this case 16 bytes of 0) to APP_CHALLENGE Characteristic
       2. Read DEVICE_CHALLENGE Characteristic Value
       3. Write the DEVICE_CHALLENGE Characteristic Value to DEVICE_RESPONSE Characteristic
          ** Since our first 8 bytes are 0, we dont need to do any crypto operations. We can just hand back the same encrypted value we get and we're authenticated.**
 - Sets up iGrillService (Needed to read probe temps)
 - Sets up iGrillBattService (Needed to read Battery Level)
 - Sets up iGrillPropaneService (Needed to read Propane Level from v3 Devices)
*/
bool connectToServer() 
{
    String temp = "Connecting to iGrill Device: " + String(myDevice->getAddress().toString().c_str());
    IGRILLLOGGER(temp ,1);
    //Setting up the iGrill Pairing Paramaters (Without Bonding you can't read the Temp Probes)
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
	  BLEDevice::setSecurityCallbacks(&oMySecurity);

    BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    //End of the iGrill Pairing Paramaters

    iGrillClient  = BLEDevice::createClient(); //Creating the iGrill Client
    if(iGrillClient == nullptr)
    {
      reScan = true;
      IGRILLLOGGER("Connection Failed!", 1);
      return false;
    }
    delay(1*1000);
    if(!iGrillClient->connect(myDevice)) //Connecting to the iGrill Device
    {
      DELETE(iGrillClient);
      reScan = true;
      IGRILLLOGGER("Connection Failed!",1);
      return false;
    }
    IGRILLLOGGER(" - Created client",1);
    iGrillClient->setClientCallbacks(&oMyClientCallback);
    // Connect to the remote BLE Server.
    IGRILLLOGGER(" - Connected to iGrill BLE Server",1);
    delay(1*1000);

    // Obtain a reference to the authentication service we are after in the remote BLE server.
    IGRILLLOGGER(" - Performing iGrill App Authentication",1);
    iGrillAuthService = iGrillClient->getService(AUTH_SERVICE_UUID);
    delay(1*1000);
    if (iGrillAuthService == nullptr) 
    {
      IGRILLLOGGER(" - Authentication Failed (iGrillAuthService is null)",1);
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
          IGRILLLOGGER(" - Authentication Failed (authRemoteCharacteristic is null)!",1);
          return false;
        }
        //Start of Authentication Sequence
        IGRILLLOGGER(" - Writing iGrill App Challenge...",1);
        authRemoteCharacteristic->writeValue((uint8_t*)chalBuf, sizeof(chalBuf), true);
        delay(500);
        IGRILLLOGGER(" - Reading iGrill Device Challenge",1);
        std::string encrypted_device_challenge = iGrillAuthService->getCharacteristic(DEVICE_CHALLENGE)->readValue();
        delay(500);
        IGRILLLOGGER(" - Writing Encrypted iGrill Device Challenge...",1);
        iGrillAuthService->getCharacteristic(DEVICE_RESPONSE)->writeValue(encrypted_device_challenge, true);
        //End of Authentication Sequence
        IGRILLLOGGER(" - Authentication Complete",1);
        if(iGrillModel == "iGrill_mini")
        {
          iGrillService = iGrillClient->getService(MINI_SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
        }        
        else if(iGrillModel == "iGrill_mini_v2")
        {
          iGrillService = iGrillClient->getService(MINIV2_SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
        }
        else if(iGrillModel == "iGrillv2")
        {
          iGrillService = iGrillClient->getService(SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
        }
        else if(iGrillModel == "iGrillv202")
        {
          iGrillService = iGrillClient->getService(V202_SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
        }
        else if(iGrillModel == "iGrillv3")
        {
          iGrillService = iGrillClient->getService(V3_SERVICE_UUID); //Obtain a reference for the Main iGrill Service that we use for Temp Probes
          delay(1*1000);
          iGrillPropaneService = iGrillClient->getService(PROPANE_SERVICE); //Obtain a reference for the iGrill Propane Service that we use for Propane Level
          if(iGrillPropaneService == nullptr)
          {
            IGRILLLOGGER(" - Unable to Setup Propane Level Service (iGrillPropaneService is null)",2);
          }
          else
          {
            IGRILLLOGGER(" - Propane Level Service Setup!",1);
            has_propane_sensor = true;
          }
        }
        delay(1*1000);
        iGrillBattService = iGrillClient->getService(BATTERY_SERVICE_UUID); //Obtain a reference for the iGrill Battery Service that we use for Getting the Battery Level
        delay(1*1000);
        connected = true;
        reScan = false;
        return true;
      }
      catch(...)
      {
        IGRILLLOGGER(" - Authentication Failed!",1);
        iGrillClient->disconnect();
        return false;
      }
    }
    else
    {
      IGRILLLOGGER(" - Authentication Failed (lost connection to device)!",1);
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
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(MINI_SERVICE_UUID)) 
    {
      BLEDevice::getScan()->stop();
      DELETE(myDevice); // delete old stuff (don't need it anymore)
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      iGrillModel = "iGrill_mini";
      doConnect = true;
    }
    else if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(MINIV2_SERVICE_UUID)) 
    {
      BLEDevice::getScan()->stop();
      DELETE(myDevice); // delete old stuff (don't need it anymore)
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      iGrillModel = "iGrill_mini_v2";
      doConnect = true;
    }
    else if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(SERVICE_UUID)) 
    {
      BLEDevice::getScan()->stop();
      DELETE(myDevice); // delete old stuff (don't need it anymore)
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      iGrillModel = "iGrillv2";
      doConnect = true;
    }
    else if(advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(V202_SERVICE_UUID))
    {
      BLEDevice::getScan()->stop();
      DELETE(myDevice); // delete old stuff (don't need it anymore)
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      iGrillModel = "iGrillv202";
      doConnect = true;
    }
    else if(advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(V3_SERVICE_UUID)) 
    {
      IGRILLLOGGER(" - iGrillv3 device discovered",2);
      BLEDevice::getScan()->stop();
      DELETE(myDevice); // delete old stuff (don't need it anymore)
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      iGrillModel = "iGrillv3";
      doConnect = true;
    }
  }
};

static MyAdvertisedDeviceCallbacks oMyAdvertisedDeviceCallbacks;

#pragma endregion
#pragma region WIFI_Fuctions

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

uint8_t connectWiFi()
{
  uint8_t status;
  LOGERROR(F("ConnectWiFi with :"));

  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
  {
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
    {
      LOGERROR3(F("* Using SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );

      LOGERROR(F("Connecting Wifi..."));
      WiFi.mode(WIFI_STA);

      status = WiFi.begin(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      status = WiFi.waitForConnectResult();

      if ( WiFi.isConnected() )
      {
        LOGERROR(F("WiFi connected"));
        LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
        LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
        return status;
      }
      else
        LOGERROR(F("No success, trying next credentials"));
    }
  }
  LOGERROR(F("WiFi not connected"));
  return status;
}

uint8_t connectMultiWiFi()
{
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

void heartBeatPrint()
{
  static int num = 1;
  if(mqtt_client != nullptr) //We have to check and see if we have a mqtt client created as this doesnt happen until the device is connected to an igrill device.
  {
    if(!mqtt_client->connected())
    {
      IGRILLLOGGER("MQTT Disconnected",2);
      connectMQTT();
    }
  }
  else //If we dont have an mqtt client set we need to initate a new BLE Scan to try and find a device.
  {
    if(num%3 == 0) //Only attempt to re-scan every 3th time we make it to the check unconnected (If using default this will be twice a min.)
      reScan = true; //Set the BLE rescan flag to true to initiate a new scan
  }
  num++;
  IGRILLLOGGER(String("free heap memory: ") + String(ESP.getFreeHeap()), 2);
}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    IGRILLLOGGER("WiFi lost.", 0);
    disconnectMQTT();
    if (NO_MULTI_WIFI)
    {
      IGRILLLOGGER("Call connectWiFi in loop", 0);
      connectWiFi();
    }
    else
    {
      IGRILLLOGGER("Call connectMultiWiFi in loop", 0);
      connectMultiWiFi();
    }
  }
}

void wifi_manager()
{
  IGRILLLOGGER("\nConfig Portal requested.", 0);
  digitalWrite(LED_BUILTIN, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
  ESP_WiFiManager ESP_wifiManager("ESP32_iGrillClient");
  
  
  IGRILLLOGGER("Opening Configuration Portal. ", 0);
  Router_SSID = ESP_wifiManager.WiFi_SSID();
  Router_Pass = ESP_wifiManager.WiFi_Pass();
  //Check if there is stored WiFi router/password credentials.
  if ( !initialConfig && (Router_SSID != "") && (Router_Pass != "") )
  {
    //If valid AP credential and not DRD, set timeout 120s.
    ESP_wifiManager.setConfigPortalTimeout(120);
    IGRILLLOGGER("Got stored Credentials. Timeout 120s", 0);
  }
  else
  {
    //If not found, device will remain in configuration mode until switched off via webserver.
    ESP_wifiManager.setConfigPortalTimeout(0);
    IGRILLLOGGER("No timeout : ", 0);
    
    if (initialConfig)
      IGRILLLOGGER("DRD or No stored Credentials..", 0);
    else
      IGRILLLOGGER("No stored Credentials.", 0);
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
  if (USE_METRIC_DEGREES)
    strcat(customhtml, " checked"); //check the box so the portal shows the correct state
  ESP_WMParameter USE_METRIC_DEGREES_CHECKBOX(USE_METRIC_DEGREES_Label, "Use Metric Degrees (&#176;C)", "T", 2, customhtml, WFM_LABEL_AFTER);

  if (NO_MULTI_WIFI)
    strcat(customhtmlNoMultiWifi, " checked"); //check the box so the portal shows the correct state
  ESP_WMParameter NO_MULTI_WIFI_CHECKBOX(NO_MULTI_WIFI_Label, "Don't use MultWifi", "T", 2, customhtmlNoMultiWifi, WFM_LABEL_AFTER);

  ESP_wifiManager.addParameter(&MQTT_SERVER_FIELD);
  ESP_wifiManager.addParameter(&MQTT_SERVERPORT_FIELD);
  ESP_wifiManager.addParameter(&MQTT_USERNAME_FIELD);
  ESP_wifiManager.addParameter(&MQTT_PASSWORD_FIELD);
  ESP_wifiManager.addParameter(&MQTT_BASETOPIC_FIELD);
  ESP_wifiManager.addParameter(&USE_METRIC_DEGREES_CHECKBOX);
  ESP_wifiManager.addParameter(&NO_MULTI_WIFI_CHECKBOX);
  ESP_wifiManager.setMinimumSignalQuality(-1);
  ESP_wifiManager.setConfigPortalChannel(0);  // Set config portal channel, Use 0 => random channel from 1-13

#if !USE_DHCP_IP    
  #if USE_CONFIGURABLE_DNS
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask, dns1IP, dns2IP);
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask);
  #endif 
#endif  
#if USING_CORS_FEATURE
  ESP_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // Start an access point and goes into a blocking loop awaiting configuration.
  // Once the user leaves the portal with the exit button processing will continue
  if (!ESP_wifiManager.startConfigPortal((const char *) ssid.c_str(), password))
  {
    IGRILLLOGGER("Not connected to WiFi but continuing anyway.", 0);
  }
  else
  {
    IGRILLLOGGER("WiFi Connected!", 0);
  }
  // Only clear then save data if CP entered and with new valid Credentials
  // No CP => stored getSSID() = ""
  if ( String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getPW(0)) != "" )
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
  USE_METRIC_DEGREES = (strncmp(USE_METRIC_DEGREES_CHECKBOX.getValue(), "T", 1) == 0);
  NO_MULTI_WIFI      = (strncmp(NO_MULTI_WIFI_CHECKBOX.getValue(), "T", 1) == 0);
  writeConfigFile();  // Writing JSON config file to flash for next boot
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn LED off as we are not in configuration mode.
}


#pragma endregion
#pragma region MQTT_Related_Functions
void disconnectMQTT()
{
  try
  {
    if (mqtt_client != nullptr)
    {
      if(mqtt_client->connected())
      {
        mqtt_client->disconnect();
      }
      DELETE(mqtt_client);
    }
  }
  catch(...)
  {
    Serial.printf("Error disconnecting MQTT\n");
  }
}

//Connect to MQTT Server
void connectMQTT() 
{
  if(iGrillMac=="")//We are not yet connected to an iGrill device we should wait to connect to MQTT until we find one.
  {
    IGRILLLOGGER("Not connected to an iGrill Device, Skipping connecting to MQTT", 1);
  }
  else
  {
    //String lastWillTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+String(ESP_getChipId(), HEX)+ "/status";
    String lastWillTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+iGrillMac+ "/status";
    IGRILLLOGGER("Connecting to MQTT...", 0);
    if (client == nullptr)
      client = new WiFiClient();
    if(mqtt_client == nullptr)
    {
      mqtt_client = new PubSubClient(*client);
      mqtt_client->setBufferSize(1024); //Needed as some JSON messages are too large for the default size
      mqtt_client->setKeepAlive(60); //Added to Stabilize MQTT Connection
      mqtt_client->setSocketTimeout(60); //Added to Stabilize MQTT Connection
      mqtt_client->setServer(custom_MQTT_SERVER, atoi(custom_MQTT_SERVERPORT));
    }
  
    if (!mqtt_client->connect(String(ESP_getChipId(), HEX).c_str(), custom_MQTT_USERNAME, custom_MQTT_PASSWORD, lastWillTopic.c_str(), 1, true, "offline"))
    {
      IGRILLLOGGER("MQTT connection failed: " + String(mqtt_client->state()), 0);
      DELETE(mqtt_client);
      delay(1*5000); //Delay for 5 seconds after a connection failure
    }
    else
    {
      IGRILLLOGGER("MQTT connected", 0);
      mqtt_client->publish(lastWillTopic.c_str(),"online");
    }
  }
}

void setDeviceJSONObject(const char * fwVersion, const char * iGrillBLEAddress)
{
  iGrillMac = String(iGrillBLEAddress); //Populate the iGrill Mac Address now that we are connected
  iGrillMac.replace(":",""); //Remove : from mac and replace with _
  DynamicJsonDocument deviceJSON(1024);
  JsonObject deviceObj = deviceJSON.createNestedObject("device");
  deviceObj["identifiers"] = iGrillMac;
  deviceObj["manufacturer"] = "Weber";
  deviceObj["model"] = iGrillModel;
  deviceObj["name"] = "igrill_"+ iGrillMac;
  deviceObj["sw_version"] = fwVersion;
  serializeJson(deviceObj, deviceStr);
}

//Publish iGrill BLE Client and connected iGrill info to MQTT
void publishSystemInfo()
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    { 
      StaticJsonDocument<512> deviceObj;
      deserializeJson(deviceObj, deviceStr);

      String payload="";
      DynamicJsonDocument sysinfoJSON(1024);
      sysinfoJSON["device"] = deviceObj;
      sysinfoJSON["name"] = "igrill_"+ iGrillMac;
      sysinfoJSON["ESP Id"] = String(ESP_getChipId(), HEX);
      sysinfoJSON["Uptime"] = getSystemUptime();
      sysinfoJSON["Network"] = WiFi.SSID();
      sysinfoJSON["Signal Strength"] = String(WiFi.RSSI());
      sysinfoJSON["IP Address"] = WiFi.localIP().toString();
      serializeJson(sysinfoJSON,payload);
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/systeminfo";
      mqtt_client->publish(topic.c_str(),payload.c_str());
      mqttAnnounce();
    }
    else
    {
      connectMQTT();
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish iGrill Temp Probe Values to MQTT
void publishProbeTemp(int probeNum, int temp)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_"+ String(probeNum);
      if(temp == -100) //If probe unplugged
      {
        if(MQTT_RETAIN_TEMP)
          mqtt_client->publish(topic.c_str(),"",true);            
        else
          mqtt_client->publish(topic.c_str(),"");
      }
      else
      {
        if(MQTT_RETAIN_TEMP)
          mqtt_client->publish(topic.c_str(),String(temp).c_str(),true);
        else
          mqtt_client->publish(topic.c_str(),String(temp).c_str());
      }
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish iGrill Battery Level to MQTT
void publishBattery(int battPercent)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/battery_level";
      mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

void publishPropaneLevel(int propanePercent)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/propane_level";
      mqtt_client->publish(topic.c_str(),String(propanePercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish MQTT Configuration Topics used by MQTT Auto Discovery
void mqttAnnounce()
{
  String battPayload ="";
  String propanePayload="";
  String p1Payload = "";
  String p2Payload = "";
  String p3Payload = "";
  String p4Payload = "";

  StaticJsonDocument<512> deviceObj;
  deserializeJson(deviceObj, deviceStr);

  DynamicJsonDocument battJSON(1024);
  battJSON["device"] = deviceObj;
  battJSON["name"] = "igrill_"+iGrillMac+" Battery Level";
  battJSON["device_class"] = "battery"; 
  battJSON["unique_id"]   = "igrill_"+iGrillMac+"_batt";
  battJSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+iGrillMac+"/battery_level";
  battJSON["unit_of_measurement"] = "%";
  serializeJson(battJSON,battPayload);

  if(has_propane_sensor)
  {
    DynamicJsonDocument proplvlJSON(1024);
    proplvlJSON["device"] = deviceObj;
    proplvlJSON["name"] = "igrill_"+iGrillMac+" Propane Level";
    proplvlJSON["unique_id"]   = "igrill_"+iGrillMac+"_prop";
    proplvlJSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+iGrillMac+"/propane_level";
    proplvlJSON["unit_of_measurement"] = "%";
    serializeJson(proplvlJSON,propanePayload);    
  }

  DynamicJsonDocument probe1JSON(1024);
  probe1JSON["device"] = deviceObj;
  probe1JSON["name"] = "igrill_"+iGrillMac+" Probe 1";
  probe1JSON["device_class"] = "temperature"; 
  probe1JSON["unique_id"]   = "igrill_"+iGrillMac+"_probe1";
  probe1JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_1";
  if(USE_METRIC_DEGREES)
    probe1JSON["unit_of_measurement"] = "°C";
  else
    probe1JSON["unit_of_measurement"] = "°F";
  if(!MQTT_RETAIN_TEMP)
  { 
    probe1JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+ "/status";
    probe1JSON["payload_available"] = "online";
    probe1JSON["payload_not_available"] = "offline";
  }
  serializeJson(probe1JSON,p1Payload);
  
  if((iGrillModel != "iGrill_mini") && (iGrillModel != "iGrill_mini_v2"))
  {
    DynamicJsonDocument probe2JSON(1024);
    probe2JSON["device"] = deviceObj;
    probe2JSON["name"] = "igrill_"+iGrillMac+" Probe 2";
    probe2JSON["device_class"] = "temperature"; 
    probe2JSON["unique_id"]   = "igrill_"+iGrillMac+"_probe2";
    probe2JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_2";
    if(USE_METRIC_DEGREES)
      probe2JSON["unit_of_measurement"] = "°C";
    else
      probe2JSON["unit_of_measurement"] = "°F"; 
    if(!MQTT_RETAIN_TEMP)
    { 
      probe2JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+ "/status";
      probe2JSON["payload_available"] = "online";
      probe2JSON["payload_not_available"] = "offline";
    }
    serializeJson(probe2JSON,p2Payload);

    DynamicJsonDocument probe3JSON(1024);
    probe3JSON["device"] = deviceObj;
    probe3JSON["name"] = "igrill_"+iGrillMac+" Probe 3";
    probe3JSON["device_class"] = "temperature"; 
    probe3JSON["unique_id"]   = "igrill_"+iGrillMac+"_probe3";
    probe3JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_3";
    if(USE_METRIC_DEGREES)
      probe3JSON["unit_of_measurement"] = "°C";
    else
      probe3JSON["unit_of_measurement"] = "°F";
    if(!MQTT_RETAIN_TEMP)
    {     
      probe3JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+ "/status";
      probe3JSON["payload_available"] = "online";
      probe3JSON["payload_not_available"] = "offline";
    }
    serializeJson(probe3JSON,p3Payload);

    DynamicJsonDocument probe4JSON(1024);
    probe4JSON["device"] = deviceObj;
    probe4JSON["name"] = "igrill_"+iGrillMac+" Probe 4";
    probe4JSON["device_class"] = "temperature"; 
    probe4JSON["unique_id"]   = "igrill_"+iGrillMac+"_probe4";
    probe4JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_4";
    if(USE_METRIC_DEGREES)
      probe4JSON["unit_of_measurement"] = "°C";
    else
      probe4JSON["unit_of_measurement"] = "°F"; 
    if(!MQTT_RETAIN_TEMP)
    { 
      probe4JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+ "/status";
      probe4JSON["payload_available"] = "online";
      probe4JSON["payload_not_available"] = "offline";
    }
    serializeJson(probe4JSON,p4Payload);
  }

  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String battConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/battery_level/config";
      mqtt_client->publish(battConfigTopic.c_str(),battPayload.c_str(),true);
      delay(100);
      String probe1ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_1/config";
      mqtt_client->publish(probe1ConfigTopic.c_str(),p1Payload.c_str(),true);
      delay(100);
      if((iGrillModel != "iGrill_mini") && (iGrillModel != "iGrill_mini_v2"))
      {
        String probe2ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_2/config";
        mqtt_client->publish(probe2ConfigTopic.c_str(),p2Payload.c_str(),true);
        delay(100);
        String probe3ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_3/config";
        mqtt_client->publish(probe3ConfigTopic.c_str(),p3Payload.c_str(),true);
        delay(100);
        String probe4ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/probe_4/config";
        mqtt_client->publish(probe4ConfigTopic.c_str(),p4Payload.c_str(),true);
        delay(100);
        if(has_propane_sensor)
        {
          String propaneLevelConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+iGrillMac+"/propane_level/config";
          mqtt_client->publish(propaneLevelConfigTopic.c_str(),propanePayload.c_str(),true);
          delay(100);
        }
      }
      //We need to publish a status of online each time we reach here otherwise probes plugged in after the initial mqtt discovery
      //will show as offline/unavailable until they see a new online announcement
      String availTopic = (String)custom_MQTT_BASETOPIC + "/sensor/igrill_"+ iGrillMac+"/status";
      mqtt_client->publish(availTopic.c_str(),"online");
      delay(100);
    }
    else
    {
      connectMQTT();
    }
  }
  else
  {
    connectMQTT();
  }
}

#pragma endregion
#pragma region ESP_Filesystem_Functions
bool readConfigFile() 
{
  File f = FileFS.open(CONFIG_FILE, "r");// this opens the config file in read-mode
  if (!f)
  {
    IGRILLLOGGER("Config File not found", 0);
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
      IGRILLLOGGER("JSON parseObject() failed", 0);
      return false;
    }  
    serializeJson(json, Serial);
    // Parse all config file parameters, override
    // local config variables with parsed values
    if (json.containsKey(MQTT_SERVER_Label))
      strcpy(custom_MQTT_SERVER, json[MQTT_SERVER_Label]);
    if (json.containsKey(MQTT_SERVERPORT_Label))
      strcpy(custom_MQTT_SERVERPORT, json[MQTT_SERVERPORT_Label]);
    if (json.containsKey(MQTT_USERNAME_Label))
      strcpy(custom_MQTT_USERNAME, json[MQTT_USERNAME_Label]);
    if (json.containsKey(MQTT_PASSWORD_Label))
      strcpy(custom_MQTT_PASSWORD, json[MQTT_PASSWORD_Label]);
    if (json.containsKey(MQTT_BASETOPIC_Label))
      strcpy(custom_MQTT_BASETOPIC, json[MQTT_BASETOPIC_Label]);
    if (json.containsKey(USE_METRIC_DEGREES_Label))
      USE_METRIC_DEGREES = json[USE_METRIC_DEGREES_Label];
    if (json.containsKey(NO_MULTI_WIFI_Label))
      NO_MULTI_WIFI = json[NO_MULTI_WIFI_Label];
  }
  IGRILLLOGGER("\nConfig File successfully parsed", 0);
  return true;
}

bool writeConfigFile() 
{
  IGRILLLOGGER("Saving Config File", 0);
  DynamicJsonDocument json(1024);
  // JSONify local configuration parameters
  json[MQTT_SERVER_Label] = custom_MQTT_SERVER;
  json[MQTT_SERVERPORT_Label] = custom_MQTT_SERVERPORT;
  json[MQTT_USERNAME_Label] = custom_MQTT_USERNAME;
  json[MQTT_PASSWORD_Label] = custom_MQTT_PASSWORD;
  json[MQTT_BASETOPIC_Label] = custom_MQTT_BASETOPIC;
  json[USE_METRIC_DEGREES_Label] = USE_METRIC_DEGREES;
  json[NO_MULTI_WIFI_Label] = NO_MULTI_WIFI;
  // Open file for writing
  File f = FileFS.open(CONFIG_FILE, "w"); 
  if (!f)
  {
    IGRILLLOGGER("Failed to open Config File for writing", 0);
    return false;
  }
  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);
  f.close();
  IGRILLLOGGER("\nConfig File successfully saved", 0);
  return true;
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

#pragma endregion
#pragma region ESP_Hardware_Related_Functions
String getSystemUptime()
{
  long millisecs = millis();
  int systemUpTimeMn = int((millisecs / (1000 * 60)) % 60);
  int systemUpTimeHr = int((millisecs / (1000 * 60 * 60)) % 24);
  int systemUpTimeDy = int((millisecs / (1000 * 60 * 60 * 24)) % 365);
  return String(systemUpTimeDy)+"d:"+String(systemUpTimeHr)+"h:"+String(systemUpTimeMn)+"m";
}

//Toggle LED State
void toggleLED()
{
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

//Event Handler Function for Button Click
static void handleClick() 
{
  IGRILLLOGGER("Button clicked!", 0);
  wifi_manager();
}

//Event Handler Function for Double Button Click
static void handleDoubleClick() 
{
  IGRILLLOGGER("Button double clicked!", 0);
}

//Event Handler Function for Button Hold
static void handleLongPressStop() 
{
  IGRILLLOGGER("Button pressed for long time and then released!", 0);
  newConfigData();
}

// Display Saved Configuration Information (Trigger by pressing and holding the reset button for a few seconds)
void newConfigData() 
{
  IGRILLLOGGER("MQTT_SERVER: " + String(custom_MQTT_SERVER), 0); 
  IGRILLLOGGER("MQTT_SERVERPORT: " + String(custom_MQTT_SERVERPORT), 0); 
  IGRILLLOGGER("MQTT_USERNAME: " + String(custom_MQTT_USERNAME), 0); 
  IGRILLLOGGER("MQTT_PASSWORD: " + String(custom_MQTT_PASSWORD), 0); 
  IGRILLLOGGER("MQTT_BASETOPIC: " + String(custom_MQTT_BASETOPIC), 0); 
  IGRILLLOGGER("Use Metric Degrees: " + String(USE_METRIC_DEGREES), 0);
  IGRILLLOGGER("Don't use MultiWifi: " + String(NO_MULTI_WIFI), 0);
}


#pragma endregion

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong LEDstatus_timeout    = 0;
  static ulong checkwifi_timeout    = 0;
  static ulong igrillheartbeat_timeout = 0;
  
  ulong current_millis = millis();

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

// Setup function
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);
  IGRILLLOGGER("Starting iGrill BLE Client using " + String(FS_Name) + " on " + String(ARDUINO_BOARD), 0);
  IGRILLLOGGER(String(ESP_WIFIMANAGER_VERSION), 0);
  IGRILLLOGGER(String(ESP_DOUBLE_RESET_DETECTOR_VERSION), 0);
  // Initialize the LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  // Format FileFS if not yet
  if (!FileFS.begin(true))
  {
    IGRILLLOGGER(String(FS_Name) + " failed! AutoFormatting.", 0);
  }

  if (!readConfigFile())
  {
    IGRILLLOGGER("Failed to read configuration file, using default values", 0);
  }

  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);

  if (!readConfigFile())
  {
    IGRILLLOGGER("Can't read Config File, using default values", 0);
  }

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (!drd)
  {
    IGRILLLOGGER("Can't instantiate. Disable DRD feature", 0);
  }
  else if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    IGRILLLOGGER("Open Config Portal without Timeout: Double Reset Detected", 0);
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
      IGRILLLOGGER("Open Config Portal without Timeout: No stored WiFi Credentials", 0);
      wifi_manager();
    }
    else if ( WiFi.status() != WL_CONNECTED ) 
    {
      if (NO_MULTI_WIFI)
      {
        IGRILLLOGGER("Call connectWiFi in setup", 0);
        connectWiFi();
      }
      else
      {
        IGRILLLOGGER("Call connectMultiWiFi in setup", 0);
        connectMultiWiFi();
      }
    }
  }
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn led off as we are not in configuration mode.
  BLEDevice::init("ESP32_iGrill");
  BLEDevice::setPower(ESP_PWR_LVL_P7);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(&oMyAdvertisedDeviceCallbacks);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  connectMQTT();
}

// Loop function
void loop()
{
  // Call the double reset detector loop method every so often, so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer consider the next reset as a double reset.
  if (drd)
    drd->loop();
  // this is just for checking if we are connected to WiFi
  // If the flag "doConnect" is true then we have scanned for and found the iGrill Device
  if (doConnect == true) 
  {
    // Now that we have found our device, lets attempt to connect and authenticate.
    if (connectToServer()) 
    {
      getiGrillInfo(); //Get and Publish to MQTT Firmware Information for iGrill Device
      setupBatteryCharacteristic(); //Setup Callbacks to get iGrill Battery Level
      setupProbes(); //Setup Callbacks to get iGrill Probe Temperatures
      if(has_propane_sensor)
        setupPropaneCharacteristic(); //Setup Callbacks to get iGrill Propane Level
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
    IGRILLLOGGER("Scanning for iGrill Devices...", 0);
    BLEDevice::getScan()->start(2, false);
    reScan = false; //Set reScan to false now that a scan has been started if a device is not found at next heartbeat a new scan will be started
  }
  check_status();
}
