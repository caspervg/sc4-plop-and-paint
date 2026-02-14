#pragma once

#include <vector>
#include <spdlog/logger.h>
#include "../shared/entities.hpp"

size_t SanitizeStrings(std::vector<Building>& allBuildings, std::vector<Prop>& allProps);
std::string SanitizeString(const std::string_view text);