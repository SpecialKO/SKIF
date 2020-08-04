#pragma once

#include <vector>
#include <stores/Steam/app_record.h>
#include <stores/Steam/vdf.h>

extern
  std::vector <
    std::pair < std::string, app_record_s >
              > apps;

extern
  std::unique_ptr <skValveDataFile> appinfo;
