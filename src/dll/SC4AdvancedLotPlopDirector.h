#pragma once

#include <cstdint>
#include <cRZCOMDllDirector.h>

class SC4AdvancedLotPlopDirector final : public cRZCOMDllDirector
{
public:
    SC4AdvancedLotPlopDirector();
    ~SC4AdvancedLotPlopDirector() override = default;

    uint32_t GetDirectorID() const override;
    bool OnStart(cIGZCOM* pCOM) override;

    bool PreFrameWorkInit() override;
    bool PreAppInit() override;
    bool PostAppInit() override;
    bool PreAppShutdown() override;
    bool PostAppShutdown() override;
    bool PostSystemServiceShutdown() override;
    bool AbortiveQuit() override;
    bool OnInstall() override;
};
