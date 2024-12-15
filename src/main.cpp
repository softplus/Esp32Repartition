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
 */

#include <Arduino.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "part_mgr.h"

WiFiManager wm;

void bindServerCallback();
void handlePartitionRead();
void handlePartitionFix();

// bind the server callbacks
void bindServerCallback(){
  wm.server->on("/partition-read", handlePartitionRead);  // add new route
  wm.server->on("/partition-fix", handlePartitionFix);  // add new route
  wm.server->on("/partition-fix", handlePartitionFix);  // add new route
}

// handle the /partition-read route
void handlePartitionRead() {
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/plain", "");
  partition_mgr_read(wm.server);
}

// handle the /partition-fix route
void handlePartitionFix() {
  if (wm.getConfigPortalActive()) {
    wm.server->send(500, "text/plain", "Cannot fix partitions while in config mode. Connect to Wifi first.");
    return;
  }
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/plain", "");
  partition_mgr_fix(wm.server);
}

// main setup function
void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP32Repartition");

    // reset settings - wipe stored credentials for testing
    // these are stored by the esp library
    //wm.resetSettings();

    // setup WifiManager for AP, custom menu
    bool res;
    wm.setTitle("Esp32Repartition");
    std::vector<const char *> menu_ids = {"wifinoscan","info","update","sep","custom"};
    wm.setMenu(menu_ids);
    wm.setCustomMenuHTML(
      "<form action='/partition-read' method='get'><button>List partitions</button></form><br/>"
      "<form action='/partition-fix' method='get'><button>Fix partitions</button></form><br/>");
    wm.setWebServerCallback(bindServerCallback);

    // Similar AP setup as WLED, but AP is 'EPM-AP', password 'wled1234', and IP 4.3.3.4
    wm.setAPStaticIPConfig(IPAddress(4,3,3,4), IPAddress(4,3,3,4), IPAddress(255,255,255,0)); // set ip,gw,sn
    res = wm.autoConnect("EPM-AP", "wled1234");

    if(!res) {
        Serial.println("Failed to connect");
        // ESP.restart(); // I mean, who really knows what to do. Retry & hope for the best.
    } 
    else {
        Serial.println("Connected to Wifi!");
    }
    wm.startWebPortal(); // Continue to run portal
}

// If you're watching the serial, this will tell you it's still working.
uint32_t nextTime;
void watchdog_loop() {
  if (millis() > nextTime) {
    Serial.print(".");
    nextTime = millis() + 30000;
  }
}

// main loop function
void loop() {
  watchdog_loop();
  wm.process(); // maintain portal
}
