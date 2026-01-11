#include "SC4AdvancedLotPlopDirector.h"

cRZCOMDllDirector* RZGetCOMDllDirector()
{
    static SC4AdvancedLotPlopDirector sDirector;
    return &sDirector;
}
