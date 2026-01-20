#pragma once
#include <array>
#include <string_view>

struct OccupantGroup {
    uint32_t id;
    std::string_view name;
};

// Constexpr array of common occupant groups from SC4Devotion wiki
inline constexpr std::array<OccupantGroup, 21> COMMON_OCCUPANT_GROUPS = {{
    {0x1000, "Residential"},
    {0x1001, "Commercial"},
    {0x1002, "Industrial"},
    {0x1003, "Transportation"},
    {0x1004, "Utility"},
    {0x1005, "Civic"},
    {0x1006, "Park"},
    {0x1300, "Rail"},
    {0x1301, "Bus"},
    {0x1302, "Subway"},
    {0x1303, "El Train"},
    {0x1500, "Police"},
    {0x1502, "Fire"},
    {0x1503, "School"},
    {0x1507, "Health"},
    {0x1508, "Airport"},
    {0x1509, "Seaport"},
    {0x1700, "Cemetery"},
    {0x1702, "Zoo"},
    {0x1906, "Stadium"},
    {0x1907, "Worship"}
}};
