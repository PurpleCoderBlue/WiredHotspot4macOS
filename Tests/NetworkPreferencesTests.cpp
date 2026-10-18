// All preferences are in-memory CF dictionaries. No real SCPreferences is opened.
#include "../Sources/NetworkService.hpp"
#include <SystemConfiguration/SystemConfiguration.h>
#include <dlfcn.h>
#include <net/if.h>
#include <unistd.h>
#include <cctype>
#include <cstdio>
#include <vector>
#include <set>
#include <iostream>

static CFMutableDictionaryRef dummyPreferences;
static CFDictionaryRef storedConfiguration;
static unsigned applyCalls = 0, commitCalls = 0;
static bool applySucceeds = false, lockAvailable = true, untrackedBridge = false;
static unsigned blockingLocks = 0;
static uid_t auditUID() { return 0; }
static SCPreferencesRef auditCreate(CFAllocatorRef, CFStringRef, CFStringRef) {
    CFRetain(dummyPreferences);
    return reinterpret_cast<SCPreferencesRef>(dummyPreferences);
}
static Boolean auditLock(SCPreferencesRef, Boolean wait) { blockingLocks += wait; return lockAvailable; }
static Boolean auditUnlock(SCPreferencesRef) { return true; }
static CFDictionaryRef auditGet(SCPreferencesRef, CFStringRef path) {
    return CFEqual(path, CFSTR("/VirtualNetworkInterfaces/Bridge/bridge7")) ? nullptr : storedConfiguration;
}
static Boolean auditSet(SCPreferencesRef, CFStringRef, CFPropertyListRef value) {
    CFRetain(value);
    CFRelease(storedConfiguration);
    storedConfiguration = static_cast<CFDictionaryRef>(value);
    return true;
}
static Boolean auditCommit(SCPreferencesRef) { ++commitCalls; return true; }
static Boolean auditApply(SCPreferencesRef) { ++applyCalls; return applySucceeds; }
static struct if_nameindex* auditInterfaces() {
    static struct if_nameindex interfaces[] = {{22, const_cast<char*>("bridge1")}, {0, nullptr}};
    static struct if_nameindex unsafe[] = {{22, const_cast<char*>("bridge1")}, {23, const_cast<char*>("bridge7")}, {0, nullptr}};
    return untrackedBridge ? unsafe : interfaces;
}
static void auditFree(struct if_nameindex*) {}
static unsigned auditIndex(const char*) { return 99; }

#define geteuid auditUID
#define SCPreferencesCreate auditCreate
#define SCPreferencesLock auditLock
#define SCPreferencesUnlock auditUnlock
#define SCPreferencesPathGetValue auditGet
#define SCPreferencesPathSetValue auditSet
#define SCPreferencesCommitChanges auditCommit
#define SCPreferencesApplyChanges auditApply
#define if_nametoindex auditIndex
#define if_nameindex auditInterfaces
#define if_freenameindex auditFree
#include "../Sources/NetworkService.cpp"

int main() {
    dummyPreferences = CFDictionaryCreateMutable(nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto config = CFDictionaryCreateMutable(nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* ownerKeys[] = {CFSTR("HoRNDISOwner")};
    const void* ownerValues[] = {CFSTR("io.github.noahhhi.horndis.network-v1")};
    auto options = CFDictionaryCreate(nullptr, ownerKeys, ownerValues, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* memberValues[] = {CFSTR("feth99")};
    auto members = CFArrayCreate(nullptr, memberValues, 1, &kCFTypeArrayCallBacks);
    CFDictionarySetValue(config, CFSTR("Options"), options);
    CFDictionarySetValue(config, CFSTR("Interfaces"), members);
    storedConfiguration = config;
    std::string error;
    bool first = horndis::setNetworkServiceMember("bridge1", "feth99", false, error);
    bool second = horndis::setNetworkServiceMember("bridge1", "feth99", false, error);
    bool failuresPersist = !first && !second && commitCalls == 1 && applyCalls == 2;
    applySucceeds = true;
    bool recovered = horndis::setNetworkServiceMember("bridge1", "feth99", false, error);
    bool pass = failuresPersist && recovered && applyCalls == 3;
    std::cout << (pass ? "PASS " : "FAIL ")
              << "committed membership is reapplied on retries; apply_attempts=" << applyCalls << '\n';
    const auto applied = applyCalls;
    untrackedBridge = true;
    pass &= !horndis::setNetworkServiceMember("bridge1", "feth99", false, error) && applyCalls == applied;
    untrackedBridge = false; lockAvailable = false;
    pass &= !horndis::setNetworkServiceMember("bridge1", "feth99", false, error) && blockingLocks == 0;
    lockAvailable = true;
    pass &= horndis::setNetworkServiceMember("bridge1", "feth99", true, error);
    applySucceeds = false;
    pass &= !horndis::setNetworkServiceMember("bridge1", "feth99", true, error);
    applySucceeds = true;
    pass &= horndis::setNetworkServiceMember("bridge1", "feth99", true, error);
    std::cout << (pass ? "PASS " : "FAIL ") << "attach retries, nonblocking lock, unrelated bridge protection\n";
    CFRelease(members); CFRelease(options);
    CFRelease(storedConfiguration); CFRelease(dummyPreferences);
    return pass ? 0 : 1;
}
