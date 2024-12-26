/**
 * @brief Main loop, setup, and server callbacks for the ESP32Repartition
 * @author John Mueller (softplus@gmail.com)
 * @version 0.1
 * @date 2024-12-15
 * 
 * @copyright Copyright (c) 2024
 * @license MIT
 */

/**
 * Creates a new wifi access point called 'EPM-AP', with password 'wled1234', and IP 4.3.3.4
 * You can list the partition table, and try to 'fix' it (resizes app0, app1 to 0x180000 bytes = 1536K)
 * 
 * Steps:
 * 1. Upload this code
 * 2. Connect to EPM-AP, set your actual wifi, it'll reboot
 * 3. Find the IP of the device, and connect to it.
 * 4. Click 'Fix partitions', and await the results.
 * .. if you're ok, it'll say ready on the bottom and reboot
 * 5. Reconnect to EPM-AP, set your wifi again, it'll reboot
 * 6. Find the IP of the device (should be the same), and connect to it.
 * 7. Upload your desired firmware update
 * 8. Good luck.
 * 
 * Note: This only works if the current partition is app0.
 * If it's app1, you need to upload a firmware update first (it can be this one).
 * 
 * Note 2: It expects a partition table with app0, app1, data in that order, nothing behind it.
 * 
 * Note 3: It empties app1 & data partitions, and resizes app0 & app1.
 * 
 * This: https://github.com/softplus/Esp32Repartition
 * WLED: https://kno.wled.ge/ & https://github.com/Aircoookie/WLED
 * PlatformIO: https://platformio.org/
 * WiFiManager: https://github.com/tzapu/WiFiManager
 * 
 */

#include <Arduino.h>
#include <WiFiManager.h>
#include "main.h"
#include "part_mgr.h"
#include "device_info.h"

WiFiManager wm;

void bindServerCallback();
void handlePartitionRead();
void handlePartitionFix();
void handleDownloadFlash(size_t start, size_t end, const char *filename);
void handleDownloadBootloader();
void handleDownloadPartition();

// bind the server callbacks
void bindServerCallback(){
  wm.server->on("/partition-read", handlePartitionRead);
  wm.server->on("/partition-fix", handlePartitionFix);
  wm.server->on("/bootloader-download", handleDownloadBootloader);
  wm.server->on("/partition-download", handleDownloadPartition);
}

// Downloads a memory section
void handleDownloadFlash(size_t start, size_t end, const char *filename) {
  DEBUG_PRINT("Downloading...\n");
  char buf[SPI_FLASH_SEC_SIZE];
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  snprintf(buf, sizeof(buf), "attachment; filename=%s", filename);
  wm.server->sendHeader("Content-Disposition", buf);
  wm.server->send(200, "application/octet-stream", "");
  for (uint32_t addr = start; addr<end; addr+=SPI_FLASH_SEC_SIZE) {
    DEBUG_PRINTF("Reading flash at 0x%x\n", addr);
    if (spi_flash_read(addr, (void *)buf, SPI_FLASH_SEC_SIZE) != ESP_OK) {
      DEBUG_PRINTF("Failed to read flash at offset 0x%x\n", addr);
      break;
    }
    wm.server->sendContent(buf, SPI_FLASH_SEC_SIZE);
  }
  DEBUG_PRINT("Done.\n");
}

// Downloads the bootloader
void handleDownloadBootloader() {
  size_t boot_addr = 0x1000;
  size_t boot_end = getPartitionTableAddr();
  handleDownloadFlash(boot_addr, boot_end, "current-bootloader.bin");
}

// Downloads the partition table
void handleDownloadPartition() {
  size_t part_addr = getPartitionTableAddr();
  handleDownloadFlash(part_addr, part_addr+SPI_FLASH_SEC_SIZE, "current-partition.bin");
}

// handle the /partition-read route
void handlePartitionRead() {
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/html", "");
  wm.server->sendContent(HTML_INTRO);
  partition_mgr_fix(wm.server, true);
  wm.server->sendContent(HTML_OUTRO);
}

// handle the /partition-fix route
void handlePartitionFix() {
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/html", "");
  wm.server->sendContent(HTML_INTRO);
  partition_mgr_fix(wm.server, false);
  wm.server->sendContent(HTML_OUTRO); // unless we already rebooted, lol
}

// main setup function
void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP32Repartition");

    // setup WifiManager for AP, custom menu
    bool res;
    wm.setTitle("Esp32Repartition");
    std::vector<const char *> menu_ids = {"custom"};
    wm.setMenu(menu_ids);

    wm.setCustomMenuHTML(
      "<script>function toggleVisible() {document.getElementById('more').style.display = "
      "  document.getElementById('more').style.display === 'none' ? 'block' : 'none';"
      "}</script>"
      "<form action='/partition-read' method='get'><button>List partitions</button></form><br/>"
      "<form action='/partition-fix' method='get'><button>Fix partitions</button></form><br/>"
      "<form action='/update' method='get'><button>Install new firmware</button></form><br/>"
      "<a id='toggle' onclick='toggleVisible()'>[ More ]</a><div id='more' style='display:none;'>"      
      "<form action='/0wifi' method='get'><button>Configure wifi settings</button></form><br/>"
      "<form action='/erase' method='get'><button>Erase wifi settings</button></form><br/>"
      "<form action='/info' method='get'><button>Device-info</button></form><br/>"
      "<form action='/bootloader-download' method='get'><button>Download bootloader</button></form><br/>"
      "<form action='/partition-download' method='get'><button>Download partition table</button></form><br/>"
      "</div><br/><br/>"
      "<a href='https://github.com/softplus/Esp32Repartition'>Esp32Repartition on Github</a><br/>"
      );

    wm.setWebServerCallback(bindServerCallback);

    // Similar AP setup as WLED, but AP is 'EPM-AP', password 'wled1234', and IP 4.3.3.4
    wm.setAPStaticIPConfig(IPAddress(4,3,3,4), IPAddress(4,3,3,4), IPAddress(255,255,255,0)); // set ip,gw,sn
    res = wm.autoConnect("EPM-AP", "wled1234");

    if(!res) {
        Serial.println("Failed to connect");
        // ESP.restart(); // I mean, who really knows what to do. Retry & hope for the best.
    } else {
        Serial.println("Connected to Wifi!");
    }
    wm.startWebPortal(); // Continue to run portal
}

// If you're watching the serial, this will tell you it's still working.
void watchdog_loop() {
  static uint32_t nextTime = 0;
  if (millis() > nextTime) {
    DEBUG_PRINT(".");
    nextTime = millis() + 30000;
  }
}

// main loop function
void loop() {
  watchdog_loop();
  wm.process(); // maintain portal
}
