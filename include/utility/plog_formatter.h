#pragma once

#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include "utility.h"

// This is a custom formatter that strips personal data from all log entries

namespace plog
{
  template<bool useUtcTime>
  class LogFormatterImpl
  {
  public:
    static util::nstring header()
    {
      return util::nstring();
    }

    static util::nstring format(const Record& record)
    {
      tm t;
      useUtcTime ? util::gmtime_s   (&t, &record.getTime().time)
                 : util::localtime_s(&t, &record.getTime().time);

      util::nostringstream ss;
      // YYYY-MM-DD
      ss << t.tm_year + 1900 << PLOG_NSTR('-') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_mon + 1 << PLOG_NSTR('-') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_mday << PLOG_NSTR(' ');

      // HH:mm:ss:m
      ss << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_hour << PLOG_NSTR(':') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_min << PLOG_NSTR(':') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_sec << PLOG_NSTR('.') << std::setfill(PLOG_NSTR('0')) << std::setw(3) << static_cast<int> (record.getTime().millitm) << PLOG_NSTR(' ');

      // Severity
      ss << std::setfill(PLOG_NSTR(' ')) << std::setw(5) << std::left << severityToString(record.getSeverity()) << PLOG_NSTR(' ');

      // Thread ID
      ss << PLOG_NSTR('[') << std::setfill(PLOG_NSTR(' ')) << std::setw(5) << std::right << record.getTid()  << PLOG_NSTR("] ");

      // Line + Function
      util::nostringstream ssFuncName;
      ssFuncName << record.getFunc() << PLOG_NSTR("] ");

      ss << PLOG_NSTR("[@") << std::setfill(PLOG_NSTR(' ')) << std::setw(5) << std::left << record.getLine() << PLOG_NSTR("] [") << std::setfill(PLOG_NSTR(' ')) << std::setw(50) << std::left << ssFuncName.str();

      // Message (stripped out of any personal data)
      ss << SKIF_Util_StripPersonalData (record.getMessage()) << PLOG_NSTR('\n');

      return ss.str();
    }
  };

  class LogFormatter        : public LogFormatterImpl<false> { };
  class LogFormatterUtcTime : public LogFormatterImpl<true>  { };
}