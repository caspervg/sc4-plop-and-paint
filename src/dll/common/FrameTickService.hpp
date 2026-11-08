#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cRZBaseSystemService.h"

// Runs a callback on the game's main thread on every framework tick, whether or not the city
// is being drawn. Register it with cIGZFrameWork::AddSystemService and AddToTick.
class FrameTickService final : public cRZBaseSystemService {
public:
    FrameTickService(const uint32_t serviceID, std::function<void()> onTick)
        : cRZBaseSystemService(serviceID, 0)
        , onTick_(std::move(onTick)) {}

    bool Init() override {
        SetServiceRunning(true);
        return true;
    }

    bool Shutdown() override {
        SetServiceRunning(false);
        return true;
    }

    bool OnTick(uint32_t) override {
        if (IsServiceRunning() && onTick_) {
            onTick_();
        }
        return true;
    }

private:
    std::function<void()> onTick_;
};
