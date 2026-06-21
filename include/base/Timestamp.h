#pragma once

/*
    时间戳工具类
    提供毫秒级时间戳获取
*/

#include <cstdint>
#include <chrono>
#include <string>

class Timestamp {
public:
    using Clock = std::chrono::system_clock;

    Timestamp() : _ms_since_epoch(0) {}

    explicit Timestamp(int64_t ms) : _ms_since_epoch(ms) {}

    static Timestamp Now() {
        auto now = Clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();
        return Timestamp(ms);
    }

    int64_t MilliSecondsSinceEpoch() const { return _ms_since_epoch; }

    // 差值（毫秒）
    int64_t operator-(const Timestamp& other) const {
        return _ms_since_epoch - other._ms_since_epoch;
    }

    bool operator<(const Timestamp& other) const {
        return _ms_since_epoch < other._ms_since_epoch;
    }

    bool operator==(const Timestamp& other) const {
        return _ms_since_epoch == other._ms_since_epoch;
    }

private:
    int64_t _ms_since_epoch;
};
