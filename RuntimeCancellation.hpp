#pragma once
#include <atomic>

struct RuntimeCancellation {
    std::atomic<bool> stop_requested{ false };
};