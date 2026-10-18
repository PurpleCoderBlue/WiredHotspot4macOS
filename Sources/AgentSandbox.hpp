// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace horndis {
// Permanently restrict this process and its descendants: a prebound BPF
// descriptor permits packet I/O, BIOCGBLEN and BIOCFLUSH, but no reconfiguration.
bool restrictDescriptorIOCTL(int descriptor, std::string& error);
}
