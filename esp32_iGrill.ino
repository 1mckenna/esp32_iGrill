#include <BLEDevice.h>

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
      Serial.printf(" ! Probe 1 Disconnected!\n");
    }
    else
    {
      Serial.printf(" * Probe 1 Temp: %d\n",pData[0]);
    }

  }
  else if(PROBE2_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      Serial.printf(" ! Probe 2 Disconnected!\n");
    }
    else
    {
      Serial.printf(" * Probe 2 Temp: %d\n",pData[0]);
    }

  }
  else if(PROBE3_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      Serial.printf(" ! Probe 3 Disconnected!\n");
    }
    else
    {
      Serial.printf(" * Probe 3 Temp: %d\n",pData[0]);
    }

  }
  else if(PROBE4_TEMPERATURE.equals(pBLERemoteCharacteristic->getUUID()))
  {
    if(pData[1] ==248) //This is the value set when probe is unplugged
    {
      Serial.printf(" ! Probe 4 Disconnected!\n");
    }
    else
    {
      Serial.printf(" * Probe 4 Temp: %d\n",pData[0]);
    }

  }
  else if(BATTERY_LEVEL.equals(pBLERemoteCharacteristic->getUUID()))
  {
    Serial.printf(" %% Battery Level: %d%%\n",pData[0]);
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
        //return false;
      }
    }
}

//Read iGrill Device Firmware
void getFirmwareVersion()
{
  Serial.printf(" - iGrill Firmware Version: %s\n", iGrillAuthService->getCharacteristic(FIRMWARE_VERSION)->readValue().c_str());
}

//Register Callback for iGrill Device Battery Level
bool setupBatteryCharacteristic()
{
  Serial.println(" - Setting up Battery Characteristic...");
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

//This it the Arduino Code that is run once per device boot.
void setup() 
{
  Serial.begin(115200);
  Serial.println("Starting iGrill BLE Client...");
  BLEDevice::init("ESP32_iGrill");
  BLEDevice::setPower(ESP_PWR_LVL_P7);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.

// This is the Arduino main loop function.
void loop() 
{
  // If the flag "doConnect" is true then we have scanned for and found the iGrill Device
  if (doConnect == true) 
  {
    // Now that we have found our device, lets attempt to connect and authenticate.
    if (connectToServer()) 
    {
      getFirmwareVersion(); //Get Firmware Information for iGrill Device
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
  delay(1*1000); // Delay a second between loops.
} // End of loop