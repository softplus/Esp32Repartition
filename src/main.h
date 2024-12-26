#ifndef MAIN_H
#define MAIN_H

// Include necessary libraries
#include <Arduino.h>

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

// used to chunk output
#define HTML_INTRO F("<html><head></head><body><div id='pre'>\n<pre>")
#define HTML_OUTRO F("</pre></div><footer><a href='/'>Home</a></footer></body></html>\n")

#endif // MAIN_H