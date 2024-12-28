#ifndef PART_MGR_H
#define PART_MGR_H

#include "WebServer.h"

size_t getPartitionTableAddr();
void partition_mgr_fix(std::unique_ptr<WebServer> & ws, bool test_only);
void getPartitionApp1(esp_partition_t *part);

#endif
