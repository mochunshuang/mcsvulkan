#include "head.hpp"

#include <iostream>
#include <memory>
#include <string_view>

using mcs::vulkan::meta::make_aggregate;

// ============================================================
// 1. 骨架：虚接口
// ============================================================
struct ILogger
{
    virtual void info(std::string_view) = 0;
    virtual void error(std::string_view) = 0;
    virtual ~ILogger() = default;
};

// ============================================================
// 2. 桥接：把虚调用转发到 aggregate 的 invoke<"name">
//    每个接口只写一次。
// ============================================================
template <class Agg>
struct LoggerBridge : ILogger
{
    Agg agg;
    explicit LoggerBridge(Agg a) : agg(std::move(a)) {}

    void info(std::string_view msg) override
    {
        agg.template invoke<"info">(msg);
    }
    void error(std::string_view msg) override
    {
        agg.template invoke<"error">(msg);
    }
};

// ============================================================
// 3. 工厂：把匿名 aggregate 擦除成 unique_ptr<ILogger>
// ============================================================
template <class Agg>
std::unique_ptr<ILogger> as_logger(Agg &&a)
{
    return std::make_unique<LoggerBridge<std::decay_t<Agg>>>(std::forward<Agg>(a));
}

// ============================================================
// 4. 使用：全程不写实现类型名
// ============================================================
int main()
{
    auto logger = as_logger(make_aggregate<"Logger", "sink", "info", "error">(
        &std::cout,
        [](auto &&self, std::string_view msg) { *self.sink << "[INFO] " << msg << '\n'; },
        [](auto &&self, std::string_view msg) {
            *self.sink << "[ERROR] " << msg << '\n';
        }));

    logger->info("started");
    logger->error("boom");
    // logger 是 ILogger*，但 impl 是 consteval 生成的匿名类型
}