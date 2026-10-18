// SPDX-License-Identifier: GPL-3.0-or-later
#include "USBDescriptors.hpp"
#include <array>
#include <set>
#include <utility>
#include <vector>

namespace horndis {
std::optional<uint8_t> selectUSBDataInterface(std::span<const uint8_t> bytes,
                                            uint8_t control,
                                            std::string& error) {
    error.clear();
    auto fail = [&](const char* message) -> std::optional<uint8_t> {
        error = message;
        return std::nullopt;
    };
    if (bytes.size() < 9 || bytes[0] < 9 || bytes[1] != 2) {
        return fail("invalid USB configuration header");
    }
    const size_t length = bytes[2] | (size_t(bytes[3]) << 8);
    if (length < bytes[0] || length > bytes.size()) {
        return fail("truncated USB configuration");
    }
    std::array<int, 256> classes;
    classes.fill(-1);
    std::set<uint8_t> networkControls;
    std::set<uint8_t> unionSlaves;
    std::vector<std::pair<unsigned, unsigned>> associations;
    std::optional<uint8_t> current;
    bool hasUnion = false;
    for (size_t cursor = bytes[0]; cursor < length;) {
        if (length - cursor < 2 || bytes[cursor] < 2 || bytes[cursor] > length - cursor) {
            return fail("invalid USB descriptor length");
        }
        const auto descriptor = bytes.subspan(cursor, bytes[cursor]);
        if (descriptor[1] == 4) {
            if (descriptor.size() < 9) return fail("truncated USB interface descriptor");
            current = descriptor[2];
            if (classes[*current] != -1 && classes[*current] != descriptor[5]) {
                return fail("conflicting USB alternate interface classes");
            }
            classes[*current] = descriptor[5];
            const auto c = descriptor[5], s = descriptor[6], p = descriptor[7];
            if ((c == 0xe0 && s == 1 && p == 3) || (c == 2 && s == 2 && p == 0xff) ||
                (c == 0xef && s == 4 && p == 1) || (c == 2 && (s == 6 || s == 0x0d))) {
                networkControls.insert(*current);
            }
        } else if (descriptor[1] == 0x24) {
            if (descriptor.size() < 3) return fail("truncated CDC functional descriptor");
            if (descriptor[2] == 6 && current == control) {
                if (descriptor.size() < 5 || descriptor[3] != control) {
                    return fail("invalid CDC union master");
                }
                hasUnion = true;
                unionSlaves.insert(descriptor.begin() + 4, descriptor.end());
            }
        } else if (descriptor[1] == 11) {
            if (descriptor.size() < 8 || descriptor[3] == 0 ||
                unsigned(descriptor[2]) + descriptor[3] > 256) {
                return fail("invalid USB interface association");
            }
            const unsigned first = descriptor[2], end = first + descriptor[3];
            if (control >= first && control < end) associations.emplace_back(first, end);
        }
        cursor += descriptor.size();
    }
    if (!networkControls.contains(control)) return fail("USB network control interface is missing");
    if (associations.size() > 1) return fail("overlapping USB interface associations");
    std::set<uint8_t> candidates;
    if (hasUnion) {
        for (auto slave : unionSlaves) {
            if (classes[slave] != 0x0a) return fail("CDC union does not reference a data interface");
            if (!associations.empty() &&
                (slave < associations[0].first || slave >= associations[0].second)) {
                return fail("CDC union references a different associated function");
            }
            candidates.insert(slave);
        }
    } else if (!associations.empty()) {
        for (unsigned i = associations[0].first; i < associations[0].second; ++i) {
            if (classes[i] == 0x0a) candidates.insert(i);
        }
    } else if (networkControls.size() == 1) {
        // Older simple Android layouts have no union/IAD. A unique data
        // interface is safe only when there is a single network function.
        for (unsigned i = 0; i < classes.size(); ++i) {
            if (classes[i] == 0x0a) candidates.insert(i);
        }
    }
    if (candidates.size() != 1) return fail("USB network data interface association is ambiguous");
    return *candidates.begin();
}
}
