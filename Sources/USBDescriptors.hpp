// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
namespace horndis {
// Select only a CDC data interface associated with this control function.
// Ambiguous or malformed configurations fail instead of claiming another function.
std::optional<uint8_t> selectUSBDataInterface(std::span<const uint8_t> configuration,
                                            uint8_t controlInterface,
                                            std::string& error);
}
