#include "SC4AdvancedLotPlopDirector.h"

#include <cIGZFrameWork.h>

namespace {
constexpr uint32_t kSC4AdvancedLotPlopDirectorID = 0xE5C2B9A7;
}

SC4AdvancedLotPlopDirector::SC4AdvancedLotPlopDirector() = default;

uint32_t SC4AdvancedLotPlopDirector::GetDirectorID() const
{
    return kSC4AdvancedLotPlopDirectorID;
}

bool SC4AdvancedLotPlopDirector::OnStart(cIGZCOM* /*pCOM*/)
{
    if (auto* framework = RZGetFrameWork()) {
        framework->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
    }
    return true;
}

bool SC4AdvancedLotPlopDirector::PreFrameWorkInit() { return true; }
bool SC4AdvancedLotPlopDirector::PreAppInit() { return true; }
bool SC4AdvancedLotPlopDirector::PostAppInit() { return true; }
bool SC4AdvancedLotPlopDirector::PreAppShutdown() { return true; }
bool SC4AdvancedLotPlopDirector::PostAppShutdown() { return true; }
bool SC4AdvancedLotPlopDirector::PostSystemServiceShutdown() { return true; }
bool SC4AdvancedLotPlopDirector::AbortiveQuit() { return true; }
bool SC4AdvancedLotPlopDirector::OnInstall() { return true; }
