# ESP32_iGrill
ESP32 BLE Client for the Weber iGrill 2 Device

This will connect to an iGrill deivce and then publish the temperatures of the probes and the iGrill Device battery level to MQTT for use in Home Automation systems.

# Status
## Complete
* Discover iGrill Devices
* Connect and Authenticate to the iGrill Device
* Read iGrill Device Firmware Version
* Read Battery Level
* Read Temperature Probes
* Detect Temperature Probe Disconnection
* Reconnect on Device Disconnection
## In Progress
* Verify iGrill Mini compatibility
* LED Status on ESP
* Wifi
* MQTT Connection
* Web Setup Interface for Wifi/MQTT

# Sample Run
```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
mode:DIO, clock div:1
load:0x3fff0018,len:4
load:0x3fff001c,len:1044
load:0x40078000,len:10124
load:0x40080400,len:5856
entry 0x400806a8
Starting iGrill BLE Client...
iGrill Device Discovered (d4:81:ca:22:33:48 -75 db)
Connecting to iGrill (d4:81:ca:22:33:48)
 - Created client
 - Connected to iGrill BLE Server
 - iGrill Pair Status: Paired
 - Performing iGrill App Authentication
 - Writing iGrill App Challenge...
 - Reading iGrill Device Challenge
 - Writing Encrypted iGrill Device Challenge...
 - Authentication Complete
 - iGrill Firmware Version: 1.5
 - Setting up Battery Characteristic...
 - Setting up Probes...
  -- Probe 1 Setup!
  -- Probe 2 Setup!
  -- Probe 3 Setup!
  -- Probe 4 Setup!
 * Probe 1 Temp: 69
 * Probe 1 Temp: 68
 * Probe 1 Temp: 69
 % Battery Level: 53%
```

# Arduino Setup Notes
1. Add https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json to the Additional Board Manager URLs.
2. In Boards Manager install esp32 version 1.0.5-rc6 (others may work, but I have only tested against this version)
