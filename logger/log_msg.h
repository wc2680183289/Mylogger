#pragma once

#include "log_common.h"
namespace logger {

struct LogMsg {
    LogMsg(SourceLocation loc, LogLevel lvl, StringView msg);
    LogMsg(LogLevel lvl, StringView msg);

    LogMsg(const LogMsg&) = default;
    LogMsg& operator=(const LogMsg&) = default;

    SourceLocation location;
    LogLevel level;
    StringView message;
};

}