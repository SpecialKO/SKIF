#include <stores/Steam/apps_list.h>

std::vector <
  std::pair < std::string, app_record_s >
            > apps;

std::unique_ptr <skValveDataFile> appinfo = nullptr;
