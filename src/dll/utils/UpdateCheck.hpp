#pragma once

#include <string>

// Asks GitHub whether a newer release is out, and offers the player its download page in an SC4 dialog.
namespace UpdateCheck
{
    // Runs on its own thread, so a slow or missing connection never holds up the game. Asks at most once
    // a day, and development builds (version 0.0.0) are skipped.
    void Start(const std::string& repository, const std::string& product, const std::string& installedVersion);

    // Shows the notice once, if the check has found a newer release by now. Call when the region view opens.
    void ShowNotice();
}
