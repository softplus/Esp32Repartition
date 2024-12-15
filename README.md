# Esp32Repartition - Fix old WLED ESP32 partition table devices via OTA

This is a kinda basic hack for updating the partition table of an ESP32 device running WLED via OTA.
This is useful for devices that have an old partition table that doesn't have enough space for the latest WLED firmware.
This is a workaround for the issue described in [this issue](https://github.com/Aircoookie/WLED/issues/4369).

*Use at your own risk.*

Risk: This can brick your ESP32 device and require a USB firmware reinstall. I am not responsible for any damage caused by this software.

## What it does

1. It will check that your partition table is in a supported order (anything, app0, app1, data); names don't matter.
2. It will check that the app0 partition is the one that is running. You can just reupdate this firmware if it's not, it will switch from app1 to app0.
3. It will erase the app1 and data partitions. *THIS WILL DELETE ALL DATA ON THESE PARTITIONS.*
4. It will resize the app0 and app1 partitions to 1536KB each. Shrinking the data partition.
5. It will recreate the checksum for the partitions.
6. It will reboot the device.
7. You can now install the latest WLED firmware via OTA.
8. Feel free to breathe again.

## Usage

1. Upload this firmware to your ESP32 device using the WLED firmware update page.
2. It will create an access point called `EPM-AP`, with password `wled1234`. Connect to it.
3. Go to `http://4.3.3.4/` in your browser. This will take you to the main menu.
4. Click `Configure Wifi` and set up your actual wifi. The device will reboot.
5. Find the IP of the device, and connect to it.
6. Click `Fix partitions`, and await the results.
7. If you're ok, it'll say ready on the bottom and reboot.
8. Reconnect to `EPM-AP`, set your wifi again, reboot, find the device and connect to it.
9. Check `Read partitions` to see if it worked.
10. Upload your desired firmware update.
11. Good luck.

## Building

OMG It was painful. Maybe it's normal to some people? Anyway.

1. Clone this repo.
2. Open PlatformIO. Let it do its thing. It will install the ESP32 platform, etc.
3. In a command window, enter `pio run -t menuconfig`. This will open the ESP32 menuconfig.
4. Enable: `Arduino Configuration` -> `Autostart Arduino setup and loop on boot`
5. Set: `Component config` -> `SPI Flash driver` -> `Writing to dangerous flash regions` -> `Allowed`
6. Save and exit.

Note you may need to follow the [workaround for Compile Error "esp32-arduino requires CONFIG_FREERTOS_HZ=1000 (currently 100)"](https://github.com/espressif/arduino-esp32/discussions/8375#discussioncomment-7908337) and manually tweak the `cmakelists.txt` file for the ESP32 platform in order to get this to compile.

Now you can build and upload the firmware.
Note uploading the firmware will reset your partition table to the old WLED partition table as shown in `partitions_old.csv`.
This allows you to test the functionality.

## Supported devices

This has only been tried on these devices. Your mileage may vary. Prepare the USB cable.

* ESP32 DevKitC
* D1 Mini ESP32

## Known issues

? Works for me.

## Changes

* 2024-12-15: Initial release - v0.1.0

## Other

Licensed under the MIT license  

*Disclaimer:*

As per the MIT license, I assume no liability for any damage to you or any other person or equipment.  
