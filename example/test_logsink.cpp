#include "console_sink.h"
#include "log_handle.h"
#include <iostream>

int main(){

    auto sink = std::make_shared<logger::ConsoleSink>();
    logger::LogHandle handle({sink});
    handle.Log(logger::LogLevel::kInfo, logger::SourceLocation(), "hello");
    return 0;
}
