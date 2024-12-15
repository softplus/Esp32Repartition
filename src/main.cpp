#include <Arduino.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "part_mgr.h"

WiFiManager wm;

void bindServerCallback();
void handlePartitionRead();
void handlePartitionFix();
void handleCustomTest();

void bindServerCallback(){
  wm.server->on("/custom", handleCustomTest);  // add new route
  wm.server->on("/partition-read", handlePartitionRead);  // add new route
  wm.server->on("/partition-fix", handlePartitionFix);  // add new route
}

void handleCustomTest() {
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/plain", "//");
  //wm.server->sendContent("<html><head><title>Custom Test</title></head><body><h1>Custom Test</h1></body></html>");
  wm.server->sendContent("char test", strlen("char test"));
  //wm.server->sendContent(String("String test"));
  wm.server->sendContent("char test2", strlen("char test2"));
  //wm.server->sendContent(String("String test2"));
  //wm.server->send(200, "text/plain", "Test2");
}

void handlePartitionRead() {
  //wm.server->send(200, "text/plain", "this works as well");
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/plain", "");
  partition_mgr_read(wm.server);
}

void handlePartitionFix() {
  //wm.server->send(200, "text/plain", "this works as well");
  wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  wm.server->send(200, "text/plain", "");
  partition_mgr_fix(wm.server);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Hello world!");

    // reset settings - wipe stored credentials for testing
    // these are stored by the esp library
    //wm.resetSettings();

    bool res;
    // config of wifimanager
    wm.setTitle("Esp32Repartition");
    std::vector<const char *> menu_ids = {"wifi","info","exit","sep","update", "sep", "custom"};
    wm.setMenu(menu_ids);
    wm.setCustomMenuHTML(
      "<form action='/custom' method='get'><button>Custom</button></form><br/>"
      "<form action='/partition-read' method='get'><button>List partitions</button></form><br/>"
      "<form action='/partition-fix' method='get'><button>Fix partitions</button></form><br/>");
    wm.setWebServerCallback(bindServerCallback);

    // Use same AP setup as WLED 
    wm.setAPStaticIPConfig(IPAddress(4,3,2,1), IPAddress(4,3,2,1), IPAddress(255,255,255,0)); // set ip,gw,sn
    res = wm.autoConnect("WLED-AP", "wled1234");

    if(!res) {
        Serial.println("Failed to connect");
        // ESP.restart();
    } 
    else {
        //if you get here you have connected to the WiFi    
        Serial.println("Connected to Wifi!");
    }
    wm.startWebPortal(); // run portal in background
}

uint32_t nextTime;
void watchdog_loop() {
  if (millis() > nextTime) {
    Serial.print(".");
    nextTime = millis() + 30000;
  }
}

void loop()
{
  watchdog_loop();
  wm.process(); // maintain portal
}

