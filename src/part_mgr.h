#ifndef _part_mgr_h
#define _part_mgr_h
#include "WebServer.h"

void partition_mgr_read(std::unique_ptr<WebServer> & ws);
void partition_mgr_fix(std::unique_ptr<WebServer> & ws);

#define DEBUG_EPM
#define DEBUGOUT Serial

#ifdef DEBUG_EPM
  #define DEBUG_PRINT(x) DEBUGOUT.print(x)
  #define DEBUG_PRINTLN(x) DEBUGOUT.println(x)
  #define DEBUG_PRINTF(x...) DEBUGOUT.printf(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x...)
#endif

#endif
