// 冒烟：按模块绑定到不同片（老师说的「分模块多实例」）

#include "log_handle.h"
#include "sharded_effective_sink.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main() {
    logger::ShardedEffectiveSink::Conf conf;
    conf.shard_count = 2;
    conf.policy = logger::ShardedEffectiveSink::RoutePolicy::kByModule;
    conf.base.dir = "/tmp/mylogger_sharded_smoke";
    conf.base.prefix = "demo";
    conf.base.pub_key =
        "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
        "FC7EDB544B869F039C";
    conf.base.single_size = logger::megabytes(1);
    conf.base.total_size = logger::megabytes(10);

    std::filesystem::remove_all(conf.base.dir);
    auto sink = std::make_shared<logger::ShardedEffectiveSink>(conf);
    // pay → 片0，login → 片1；之后同模块日志进同一条流水线（片内有序）
    sink->BindModule("pay", 0);
    sink->BindModule("login", 1);

    logger::LogHandle handle({sink});
    logger::SourceLocation loc(__FILE__, __LINE__, __func__);

    logger::ShardedEffectiveSink::SetThreadModule("pay");
    for (int i = 0; i < 100; ++i) {
        handle.Log(logger::LogLevel::kInfo, loc, "pay event " + std::to_string(i));
    }
    logger::ShardedEffectiveSink::SetThreadModule("login");
    for (int i = 0; i < 100; ++i) {
        handle.Log(logger::LogLevel::kInfo, loc, "login event " + std::to_string(i));
    }
    logger::ShardedEffectiveSink::ClearThreadModule();

    sink->Flush();
    auto st = sink->GetStats();
    std::cout << "片数=" << sink->shard_count() << " 队列丢弃=" << st.dropped_by_queue
              << " 缓存丢弃=" << st.dropped_by_cache << " 完成，输出目录 " << conf.base.dir << "\n";
    return 0;
}
