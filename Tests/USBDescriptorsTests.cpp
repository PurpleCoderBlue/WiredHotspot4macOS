// SPDX-License-Identifier: GPL-3.0-or-later
#include "USBDescriptors.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <random>

static std::vector<uint8_t> configuration(std::initializer_list<uint8_t> body) {
    std::vector<uint8_t> bytes{9, 2, 0, 0, 4, 1, 0, 0x80, 50};
    bytes.insert(bytes.end(), body);
    bytes[2] = bytes.size() & 255;
    bytes[3] = bytes.size() >> 8;
    return bytes;
}
int main() {
    // ECM 0/1 and RNDIS 2/3, intentionally not in numeric descriptor order.
    auto composite = configuration({
        9,4,2,0,0,0xe0,1,3,0, 5,0x24,6,2,3,
        9,4,1,0,2,0x0a,0,0,0,
        9,4,0,0,0,2,6,0,0, 5,0x24,6,0,1,
        9,4,3,0,2,0x0a,0,0,0});
    std::string error;
    assert(horndis::selectUSBDataInterface(composite, 2, error) == 3);
    assert(horndis::selectUSBDataInterface(composite, 0, error) == 1);
    auto iad = configuration({8,11,2,2,0xe0,1,3,0,
        9,4,2,0,0,0xe0,1,3,0, 9,4,3,0,2,0x0a,0,0,0});
    assert(horndis::selectUSBDataInterface(iad, 2, error) == 3);
    auto single = configuration({9,4,0,0,0,0xe0,1,3,0, 9,4,1,0,2,0x0a,0,0,0});
    assert(horndis::selectUSBDataInterface(single, 0, error) == 1);
    auto ambiguous = configuration({9,4,2,0,0,0xe0,1,3,0,
        9,4,1,0,2,0x0a,0,0,0, 9,4,3,0,2,0x0a,0,0,0});
    assert(!horndis::selectUSBDataInterface(ambiguous, 2, error));
    auto truncated = composite; truncated.pop_back();
    assert(!horndis::selectUSBDataInterface(truncated, 2, error));
    auto zeroLength = single; zeroLength[9] = 0;
    assert(!horndis::selectUSBDataInterface(zeroLength, 0, error));
    auto wrongSlave = composite; wrongSlave[22] = 0; // union points to a control interface
    assert(!horndis::selectUSBDataInterface(wrongSlave, 2, error));
    auto wrongAssociation = configuration({8,11,2,2,0xe0,1,3,0,
        9,4,2,0,0,0xe0,1,3,0, 5,0x24,6,2,1,
        9,4,1,0,2,0x0a,0,0,0, 9,4,3,0,2,0x0a,0,0,0});
    assert(!horndis::selectUSBDataInterface(wrongAssociation, 2, error));
    std::mt19937 random(0x55425344);
    for (unsigned i = 0; i < 20000; ++i) {
        std::vector<uint8_t> bytes(random() % 512);
        for (auto& byte : bytes) byte = static_cast<uint8_t>(random());
        if (bytes.size() >= 9) {
            bytes[0] = 9; bytes[1] = 2;
            bytes[2] = bytes.size() & 255; bytes[3] = bytes.size() >> 8;
        }
        (void)horndis::selectUSBDataInterface(bytes, random() % 256, error);
        auto mutated = composite;
        mutated[random() % mutated.size()] = static_cast<uint8_t>(random());
        (void)horndis::selectUSBDataInterface(mutated, 2, error);
    }
    std::cout << "USB descriptor association and malformed descriptor tests passed\n";
}
