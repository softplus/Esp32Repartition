#ifndef MAIN_H
#define MAIN_H

// Include necessary libraries
#include <Arduino.h>

// This is what the app partitions will be resized to.
// Adjust this as needed.
// Note that you cannot shrink app partitions, you can only expand them.
// All app partitions will be made the same size.
// The biggest data partition will be shrunk do adjust.
// Partitions may be moved as needed.
#define RESIZE_APP_PARTITION_SIZE 0x180000 // (1536K)
// ^^^ adjust this as needed ^^^
//

#define DEBUG_MODE // or comment out to disable debug output
#define DEBUGOUT Serial // where to send debug output

#ifdef DEBUG_MODE
  #define DEBUG_PRINT(x) DEBUGOUT.print(x)
  #define DEBUG_PRINTLN(x) DEBUGOUT.println(x)
  #define DEBUG_PRINTF(x...) DEBUGOUT.printf(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x...)
#endif

// used to output log as HTML
#define HTML_INTRO F("<!DOCTYPE html><html><head><meta charset='utf-8' />" \
  "<meta name='viewport' content='width=device-width, initial-scale=1' />" \
  "<title>ESP32Repartition</title></head><body><div id='main'><pre>")

#define HTML_OUTRO F("</pre></div><footer><a href='/'>Home</a></footer>" \
  "<script>function downloadPageText() {" \
  "const t = document.body.innerText; " \
  "const a = document.createElement('a');" \
  "a.href = URL.createObjectURL(new Blob([t], {type: 'text/plain'})); " \
  "a.download = 'page-content.txt'; a.click(); " \
  "} </script>" \
  "<br/>Need a record? Save this log locally: <button onclick='downloadPageText()'>Download log</button>" \
  "</body></html>\n")

#endif // MAIN_H