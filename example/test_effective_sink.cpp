#include "effective_sink.h"
#include "log_handle.h"
#include <iostream>

int main() {
  logger::EffectiveSink::Conf conf;
  conf.dir = "/tmp/mylogger_test";
  conf.prefix = "demo";
  conf.pub_key =
      "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
      "FC7EDB544B869F039C";
  conf.single_size = logger::megabytes(1);
  conf.total_size = logger::megabytes(10);

  auto sink = std::make_shared<logger::EffectiveSink>(conf);
  logger::LogHandle handle({sink});

  for (int i = 0; i < 1000; ++i) {
    handle.Log(logger::LogLevel::kInfo, logger::SourceLocation(__FILE__, __LINE__, __func__),
               "hello effective sink " + std::to_string(i));
  }
  sink->Flush();
  std::cout << "done, check " << conf.dir << std::endl;
  return 0;
}