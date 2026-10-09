#pragma once

#include <cstdint>
#include <string>

bool parse_mode(const std::string& specification, std::uint16_t current, bool directory,
                std::uint16_t& result, std::string& error);
std::string format_mode(std::uint16_t mode, bool directory);
