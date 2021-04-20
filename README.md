# ESP32_iGrill
ESP32 BLE Client for the Weber iGrillv2 and v3 Devices

This will connect to an iGrill deivce and then publish the temperatures of the probes and the iGrill Device battery level to MQTT for use in Home Automation systems.

# Table of Contents
- [ESP32_iGrill](#esp32-igrill)
- [Arduino IDE Setup](#arduino-ide-setup)
  * [Install ESP32 Board Support with latest BLE Libs](#install-esp32-board-support-with-latest-ble-libs)
  * [Select the ESP32 Board](#select-the-esp32-board)
  * [Select the Correct Partition Scheme](#select-the-correct-partition-scheme)
  * [Install Required Libraries](#install-required-libraries)
- [Clone and Flash](#clone-and-flash)
  * [Clone the Repo](#clone-the-repo)
  * [Open and Flash the Project](#open-and-flash-the-project)
- [Initial Configuration](#initial-configuration)
  * [Configuration Portal Connection Information](#configuration-portal-connection-information)
  * [Default Configuration Portal Settings](#default-configuration-portal-settings)
  * [Initial Configuration Walkthrough](#initial-configuration-walkthrough)
- [Home Assistant Information](#home-assistant-information)
  * [Device View](#device-view)
  * [Detailed View](#detailed-view)
  * [Sample HA Dashboard](#sample-ha-dashboard)
    + [Setup Steps](#setup-steps)
    + [iGrill Device Connected](#igrill-device-connected)
    + [iGrill Device Disconnected](#igrill-device-disconnected)
- [MQTT Information](#mqtt-information)
  * [MQTT Topics Published](#mqtt-topics-published)
  * [MQTT Retained Topics](#mqtt-retained-topics)
- [Troubleshooting](#troubleshooting)
  * [Common Issues](#common-issues)
  * [Change Serial Logging Level](#change-serial-logging-level)
- [iGrill Client Development Status](#igrill-client-development-status)
  * [In Progress](#in-progress)
  * [Completed](#completed)
- [Contributions](#Contributions)

# Arduino IDE Setup
## Install ESP32 Board Support with latest BLE Libs
1. Open Arduino IDE
2. Click <b>File &#8594; Preferences</b>
3. Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json` to the Additional Board Manager URLs.
![add_esp32_board_url](https://github.com/1mckenna/esp32_iGrill/blob/main/images/add_esp32_board_url.png?raw=true)
4. Click <b>OK</b>
5. From the toolbar select <b>Tools &#8594; Board "<i>Currently_Selected_Board</i>" &#8594; Boards Manager...</b>
![open_boardmanager](https://github.com/1mckenna/esp32_iGrill/blob/main/images/open_boardmanager.png?raw=true)
6. In the Boards Manager Dialog Window Search for <b>esp32</b>
7. After a few seconds you should see esp32 by Espressif Systems
8. Select the version you want to install (tested with 1.0.5-rc6)
9. Click Install
![install_boardmanager](https://github.com/1mckenna/esp32_iGrill/blob/main/images/install_boardmanager.png?raw=true)

## Select the ESP32 Board
1. Click <b>Tools &#8594; Board &#8594; ESP32 Arduino &#8594; ESP32 Dev Module</b>
![select_esp32_board](https://github.com/1mckenna/esp32_iGrill/blob/main/images/select_esp32_board.png?raw=true)

## Select the Correct Partition Scheme
We need to change from the default parition scheme, becuase we utilize both Wifi Manager and Bluetooth libraries. The combination of both are too large for the default partition scheme.
1. Click <b>Tools &#8594; Partition Scheme &#8594; Huge APP (3MB No OTA/SPIFFS)</b>
![select_esp32_partition](https://github.com/1mckenna/esp32_iGrill/blob/main/images/select_esp32_partition.png?raw=true)
## Install Required Libraries
1. Open the Libray Manager
2. Click <b>Tools &#8594; Manage Libraries...</b>
![open_libmanager](https://github.com/1mckenna/esp32_iGrill/blob/main/images/open_libmanager.png?raw=true)
3. Search for and install the following libraries
    * ArdunioJson
    ![arduniojson](https://github.com/1mckenna/esp32_iGrill/blob/main/images/install_arduinojson.png?raw=true)
    * PubSubClient
    ![pubsubclient](https://github.com/1mckenna/esp32_iGrill/blob/main/images/install_pubsubclient.png?raw=true)
    * ESP_WifiManager
    ![esp_wifimanager](https://github.com/1mckenna/esp32_iGrill/blob/main/images/install_esp_wifimgr.png?raw=true)
    * ESP_DoubleResetDetector (you will be prompted to install automatically)
    * LittleFS_esp32 (you will be prompted to install automatically)
    ![esp_wifimanager_deps](https://github.com/1mckenna/esp32_iGrill/blob/main/images/install_esp_wifimgr_deps.png?raw=true)

# Clone and Flash
If you have not yet completed the Arduino IDE setup steps or have chosen to skip these steps, ensure you have the correct partition size selected and all the libraries necessary already installed.
## Clone the Repo
1. Open the termnial and cd to your Arduino project folder
    * `cd ~/Ardunio`
2. Clone this repo
    * `git clone https://github.com/1mckenna/esp32_iGrill.git`

## Open and Flash the Project
1. Open the Ardunio IDE
2. Open the esp32_iGrill Sketch</br>
    ![open_sketch](https://github.com/1mckenna/esp32_iGrill/blob/main/images/open_sketch.png?raw=true)
3. Verify</br>
    ![arduino_verify](https://github.com/1mckenna/esp32_iGrill/blob/main/images/arduino_verify.png?raw=true)
4. Upload</br>  
    ![arduino_upload](https://github.com/1mckenna/esp32_iGrill/blob/main/images/arduino_upload.png?raw=true)

# Initial Configuration
After flashing the ESP Device the first time the device will automatically enter configuration mode.

When the device is in configuarion mode it will start a wireless access point named, <b>iGrillClient_<i>esp32ChipID</i></b> and the Blue LED will stay solid blue.

## Configuration Portal Connection Information
| **Wifi SSID Name** | **Wifi Password**
| :--------------------: | :--------------------: |
|  iGrillClient_<i>esp32ChipID</i> | igrill_client |
</br>

## Default Configuration Portal Settings
| **Setting** | **Default Value** | **Comment** |
| :--------------------: | :--------------------: | :--------------------: |
| SSID Name | blank | Primary Wifi Network Name <b>(required)</b> |
| SSID Password | blank | Primary Wifi Network Password <b>(required)</b>|
| SSID1 Name | blank | Secondary Wifi Network Name </br><i>If you only want to connect the device to a single network you can leave this blank</i>| |
| SSID1 Password | blank | Secondary Wifi Network Password </br><i>If you only want to connect the device to a single network you can leave this blank</i>|
| MQTT_SERVER | 127.0.0.1 | Change to your MQTT Broker IP |
| MQTT_SERVERPORT | 1883 | Change to your MQTT Broker Port|
| MQTT_USERNAME | mqtt | Change to your MQTT Username|
| MQTT_PASSWORD | password | Change to your MQTT User Password |
| MQTT_BASETOPIC | igrill | Change to your desired base MQTT Topic</br>If you are using Home Assistant and want to take advantage of MQTT Autodiscovery you need to set this to your mqtt autodiscoervy prefix. </br><i>(Home Assistant Default: <b>homeassistant</b>)</i>|

## Initial Configuration Walkthrough
1. Put the device in configuration mode (Press the reset button 2x in quick succession). The blue LED will stay solid once configururation mode has been entered.
2. Scan for the new iGrillClient Configuration Wireless network</br>
    ![wm_connect_1](https://github.com/1mckenna/esp32_iGrill/blob/main/images/wm_connect_1.png?raw=true)
3.  Connect to the device using the password <b>igrill_client</b></br>
    ![wm_connect_pw](https://github.com/1mckenna/esp32_iGrill/blob/main/images/wm_connect_pw.png?raw=true)
4. Once you have connected you will be brought to the Main configuration page. 
   * If you have connected to a network previously you will see similar information as shown in the yellow boxes.
   * Select Configuration to be taken to the device configuration page.</br>
    ![wm_config_main](https://github.com/1mckenna/esp32_iGrill/blob/main/images/wm_config_main.png?raw=true)
5. You should now be at the main configuration page.</br>
<span style="color:red"><i>Note: Use of a hidden SSID is unsupported (broken)</i></span></br>
   * Discovered networks will show up in the area noted by the green box.
   * By selecting a discovered network it will autopopulate that name into the SSID field.
   * If you only have one network you can leave SSID1 and its corresponding password field blank
   * Populate the missing MQTT Information
   * Choose Imperial or Metric Units for Degrees (make sure to match the setting on your iGrill device)</br>
    ![wm_wifi_mqtt](https://github.com/1mckenna/esp32_iGrill/blob/main/images/wm_wifi_mqtt.png?raw=true)    
6. Configure IP Settings
   * <b>Leave the current values shown alone if you wish to use DHCP.</b>
   * If you wish to use a static IP Address <b>instead of</b> DHCP please complete the Static IP, Gateway IP and Subnet fields prior to pressing Save</br>
    ![wm_wifi_ip](https://github.com/1mckenna/esp32_iGrill/blob/main/images/wm_wifi_ip.png?raw=true)



# Home Assistant Information
After you have configured the esp32_iGrill Client and have it connected to the same MQTT Broker as your Home Assistant instance the device should automatically appear.
## Device View
![igrill_ha_device](https://github.com/1mckenna/esp32_iGrill/blob/main/images/igrill_ha_device.png?raw=true)
## Detailed View
![igrill_ha_device_details](https://github.com/1mckenna/esp32_iGrill/blob/main/images/igrill_ha_device_details.png?raw=true)
## Sample HA Dashboard
Below are some images of a sample Home Assistant Dashboard for the iGrill. 
(Dashboard based off the work of stogs and Jerrit in this [thread](https://community.home-assistant.io/t/weber-igrill-2-integration-with-lovelace-ui/61880/184))

### Setup Steps

All the configs needed to setup the dashboard have been placed in the ha_example_config directory.</br> The only thing you should have to change in the **ALL** the files below is the mac address shown to match your iGrill device.
 1. Copy the contents of ha_example_config/automations/igrill.yaml into your automations.yaml
 2. Copy the contents of ha_example_config/sensors/igrill.yaml into your configuration.yaml or your sensors.yaml
 3. Copy the contents of ha_example_config/input_numbers/igrill.yaml into your configuration.yaml or input_numbers.yaml
 4. Copy the contents of ha_example_config/lovelace-ui-igrill.yaml into your lovelace-ui.yaml or copy the contents to the clipboard, put your Home Assistant Dashboard into Edit Mode, then open the Raw Configuration editor and paste in the contents.

### iGrill Device Connected
![ha_sample_dashboard.png](https://github.com/1mckenna/esp32_iGrill/blob/main/images/ha_sample_dashboard.png?raw=true)

### iGrill Device Disconnected
![ha_sample_dashboard_disconnected.png](https://github.com/1mckenna/esp32_iGrill/blob/main/images/ha_sample_dashboard_disconnected.png?raw=true)

## Other Notes
  * You can replace the ESP32 used to talk to the iGrill without needing to make any changes on the Home Assistant side. This is why we use the iGrill MAC in the MQTT topics instead of relying on the ESP32 Device MAC.
  * The Battery Level shown for the device in Home Assistant
    * The value is the last known battery_level recieved via MQTT
    * Will have its value persist throughout restarts of Home Assistant as it and the configuration topics have the retain flag set

# MQTT Information
## MQTT Topics Published
| **MQTT Topic** | **Value(s)** |
| :--------------------: | :--------------------: |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/status | online: MQTT Connected</br>offline: MQTT Disconnected |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/systeminfo | System Information about iGrill BLE Client Device</br><i>(iGrill Device Name, ESP32 Chip ID, Uptime, Wifi Network, Wifi Signal Strength, IP Address)</i>|
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_1 | Temperature Value of Probe 1 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_2 | Temperature Value of Probe 2 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_3 | Temperature Value of Probe 3 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_4 | Temperature Value of Probe 4 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/battery_level | Battery Level of the iGrill Device |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_1/config | MQTT Autoconfiguration Settings for Probe 1 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_2/config | MQTT Autoconfiguration Settings for Probe 2 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_3/config | MQTT Autoconfiguration Settings for Probe 3 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_4/config | MQTT Autoconfiguration Settings for Probe 4 |
|MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/battery_level/config | MQTT Autoconfiguration Settings for Battery Level |

## MQTT Retained Topics
We only set the retain flag on the following topics
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/battery_level  <b><i>(We set the retain flag on this so we know the level of the iGrill battery the last time the device was seen)</b></i>
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/battery_level/config
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_1/config
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_2/config
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_3/config
  * MQTT_BASETOPIC/sensor/igrill_<i>iGrillMAC</i>/probe_4/config


# Troubleshooting
## Common Issues
  * The device is in Configuration Mode after flashing
    * Don't Worry you havent lost your configuration
    * This can happen due to the reset of the board after flashing getting detected as a second press of the reset button. Just press the reset button on hte board once to reboot the device out of configuration mode.
  * I cannot connect to the iGrill Device
    * iGrill Devices can only be connected to a single device at a time. For best results unpair the iGrill from all phones/tablets that it has been conncetd to in the past.
  * Home Assistant is not showing the correct temperatures but the temperature shown on the device is correct.
    * Make sure the Use Metric Degrees setting in the configuration portal matches the setting on your iGrill device.

## Change Serial Logging Level
If you are running into an issue and want to increase the verbosity of the logging that can be done via the following two settings in <b>config.h</b>
  * `_WIFIMGR_LOGLEVEL_` <i>(Default: 1)</i>
    * Use from 0 to 4. Higher number, more debugging messages and memory usage.
  * `IGRILL_DEBUG_LVL` <i>(Default: 1)</i>
    * Level 0: Print only Basic Info
    * Level 1: Level 0 + Print BLE Connection Info (Default)
    * Level 2: Level 1 + Print everything including temp/batt callbacks (Only Recommended for troubleshooting BLE issues)

# iGrill Client Development Status
## In Progress
* Add support for iGrill Mini (Need a tester)

## Completed
* AutoDiscover iGrill Devices
* Support for iGrillv3
* Connect and Authenticate to the iGrill Device
* Read iGrill Device Firmware Version
* Read Battery Level
* Read Temperature Probes
* Detect Temperature Probe Disconnection
* Reconnect on Device Disconnection
* LED Status on ESP
* Wifi
* MQTT Connection
* MQTT Auto Discovery
* Web Setup Interface for Wifi/MQTT

# Contributions
 * [beed2112](https://github.com/beed2112) &#8594;  MQTT Design Consult / Massive Amounts of Testing
 * [dev-strom](https://github.com/dev-strom) &#8594; [Bug Fixes / Addition of option to disable MultiWifi ](https://github.com/1mckenna/esp32_iGrill/issues/3)