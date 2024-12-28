# Esp32Repartition - Fix old WLED ESP32 partition table devices via OTA

This is a kinda basic hack for updating the partition table of an ESP32 device running [WLED](https://kno.wled.ge/) via OTA.
This creates a 1536KB partition for app0 and app1.
This is useful for devices that have an old partition table that doesn't have enough space for the latest WLED firmware.
This is a workaround for the issue described in [this issue](https://github.com/Aircoookie/WLED/issues/4369).

*Use at your own risk.*

Risk: This can brick your ESP32 device and require a USB firmware reinstall. I am not responsible for any damage caused by this software.
This code is not tested on many devices. Use at your own risk, really.
Also, make sure your device doesn't randomly get rebooted / have bad power;
if it stops at the wrong time (the window is short), it can disable your partition table and require a USB firmware reinstall.

## What it does

1. It will check that your partition table is in a supported order (anything, app0, app1, data); names don't matter.
2. It will check that the app0 partition is the one that is running. You can just reupdate this firmware if it's not, it will switch from app1 to app0.
3. It will erase the app1 and data partitions. *THIS WILL DELETE ALL DATA ON THESE PARTITIONS.*
4. It will resize the app0 and app1 partitions to 1536KB each. Shrinking the data partition.
5. It will recreate the checksum for the partitions.
6. It will reboot the device.
7. You can now install the latest WLED firmware via OTA.
8. Connect to 'WLED-AP' and set up your wifi.
9. Feel free to breathe again.

## Usage

1. Backup both `Prefixes` and `Configuration` from your WLED setup. This data will get lost.
2. Upload this [firmware](https://github.com/softplus/Esp32Repartition/releases) to your ESP32 device using the WLED firmware update page.
3. It will create an access point called `EPM-AP`, with password `wled1234`. Connect to it.
4. Go to `http://4.3.3.4/` in your browser. This will take you to the main menu.
7. Click `List partitions` to see the current partition table.
    It does a bunch of tests to make sure that your device is suitable for this. In particular:
    * It checks that you're in the first `app` partition. Otherwise just upload the same firmware again and try again.
    * It checks that the partition table is a supported layout (app0, app1, data).
    * It checks that the data partition has enough space.
    * It shows you the current partition table & the proposed new one.
    * FYI, example of a 'reasonable' partition table:
    ```
    Partition:
    Addr: 0x00008000
    Type: 01, Subtype: 02, Addr: 0x00009000, Size: 0x00005000 (20K), Label: nvs
    Type: 01, Subtype: 00, Addr: 0x0000e000, Size: 0x00002000 (8K), Label: otadata
    Type: 00, Subtype: 10, Addr: 0x00010000, Size: 0x00140000 (1280K), Label: app0
    Type: 00, Subtype: 11, Addr: 0x00150000, Size: 0x00140000 (1280K), Label: app1
    Type: 01, Subtype: 82, Addr: 0x00290000, Size: 0x00170000 (1472K), Label: spiffs
    Running: Addr: 0x00010000, Label: app0
    Next:    Addr: 0x00150000, Label: app1
    ```
8. Click `Fix partitions`, and await the results.
9. If you're ok, it'll say ready on the bottom and reboot.
    Click `Download log` to save what you see to a local text file.
    If something breaks, you (or I) might find it useful.
10. Reload the page once reboot is complete.
11. Check `List partitions` to see if it worked.
12. Upload your desired firmware update. Connect to WLED's AP, set your wifi settings, restore prefixes and configuration.
13. Good luck.

## Building

Setting everything up took a bit, but here's roughly how it works:

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

Potential to-dos:

* Find a bunch of bootloaders & check their MD5

## Changes

[Releases](https://github.com/softplus/Esp32Repartition/releases)

* 2024-12-28: Release v0.4.0
  * Checks for encrypted flash & aborts if so
  * Added support to download app1 partition
  * Added timing of steps
* 2024-12-27: Release v0.3.0
  * Add support for various configurations with min 2x app, 1x data
  * Move data partitions instead of deleting
* 2024-12-27: Release v0.2.0
* 2024-12-15: Initial release - v0.1.0

## My notes

For releases:

```bash
cp .pio/build/esp32dev/firmware.bin build/esp32-repart.bin
```

## Other

Licensed under the MIT license.
[WLED](https://kno.wled.ge/) is awesome and you should check it out.

*Disclaimer:*

As per the MIT license, I assume no liability for any damage to you or any other person or equipment.  
