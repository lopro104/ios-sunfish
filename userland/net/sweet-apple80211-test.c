#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

int main(int argc, char **argv) {
    printf("=== Testing Apple80211 & MobileWiFi HAL ===\n");

    /* 1. Test IO80211.framework */
    void *h_io80211 = dlopen("/System/Library/PrivateFrameworks/IO80211.framework/IO80211", RTLD_NOW);
    if (!h_io80211) {
        printf("Failed to dlopen IO80211.framework: %s\n", dlerror());
    } else {
        printf("Successfully loaded IO80211.framework\n");
        int (*Apple80211Open)(void **handle) = dlsym(h_io80211, "Apple80211Open");
        int (*Apple80211Close)(void *handle) = dlsym(h_io80211, "Apple80211Close");
        int (*Apple80211GetIfListCopy)(void *handle, CFArrayRef *ifList) = dlsym(h_io80211, "Apple80211GetIfListCopy");
        int (*Apple80211BindToInterface)(void *handle, CFStringRef ifname) = dlsym(h_io80211, "Apple80211BindToInterface");
        const char* (*Apple80211ReturnToString)(int err) = dlsym(h_io80211, "Apple80211ReturnToString");

        if (Apple80211Open && Apple80211GetIfListCopy) {
            void *handle = NULL;
            int ret = Apple80211Open(&handle);
            printf("Apple80211Open -> %d (handle = %p)\n", ret, handle);
            if (ret == 0 && handle) {
                CFArrayRef ifList = NULL;
                ret = Apple80211GetIfListCopy(handle, &ifList);
                const char *errStr = Apple80211ReturnToString ? Apple80211ReturnToString(ret) : "unknown";
                printf("Apple80211GetIfListCopy -> %d (%s)\n", ret, errStr);
                if (ifList) {
                    CFIndex count = CFArrayGetCount(ifList);
                    printf("Found %ld interfaces:\n", (long)count);
                    for (CFIndex i = 0; i < count; i++) {
                        CFStringRef ifname = (CFStringRef)CFArrayGetValueAtIndex(ifList, i);
                        char buf[128];
                        if (CFStringGetCString(ifname, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                            printf("  Interface %ld: %s\n", (long)i, buf);
                        }
                    }
                    CFRelease(ifList);
                } else {
                    printf("ifList is NULL\n");
                }

                if (Apple80211BindToInterface) {
                    CFStringRef en0 = CFStringCreateWithCString(kCFAllocatorDefault, "en0", kCFStringEncodingUTF8);
                    ret = Apple80211BindToInterface(handle, en0);
                    const char *bindErr = Apple80211ReturnToString ? Apple80211ReturnToString(ret) : "unknown";
                    printf("Apple80211BindToInterface(en0) -> %d (%s)\n", ret, bindErr);
                    CFRelease(en0);
                }
                if (Apple80211Close) {
                    Apple80211Close(handle);
                }
            }
        }
    }

    /* 2. Test MobileWiFi.framework */
    void *h_mowifi = dlopen("/System/Library/PrivateFrameworks/MobileWiFi.framework/MobileWiFi", RTLD_NOW);
    if (!h_mowifi) {
        printf("Failed to dlopen MobileWiFi.framework: %s\n", dlerror());
    } else {
        printf("\nSuccessfully loaded MobileWiFi.framework\n");
        void* (*WiFiManagerClientCreate)(CFAllocatorRef allocator, int flags) = dlsym(h_mowifi, "WiFiManagerClientCreate");
        CFArrayRef (*WiFiManagerClientCopyInterfaces)(void *manager) = dlsym(h_mowifi, "WiFiManagerClientCopyInterfaces");
        CFArrayRef (*WiFiManagerClientCopyDevices)(void *manager) = dlsym(h_mowifi, "WiFiManagerClientCopyDevices");
        CFStringRef (*WiFiDeviceClientGetInterfaceName)(void *dev) = dlsym(h_mowifi, "WiFiDeviceClientGetInterfaceName");
        int (*WiFiDeviceClientGetPower)(void *dev) = dlsym(h_mowifi, "WiFiDeviceClientGetPower");

        if (WiFiManagerClientCreate) {
            void *mgr = WiFiManagerClientCreate(kCFAllocatorDefault, 0);
            printf("WiFiManagerClientCreate -> %p\n", mgr);
            if (mgr) {
                if (WiFiManagerClientCopyInterfaces) {
                    CFArrayRef ifaces = WiFiManagerClientCopyInterfaces(mgr);
                    printf("WiFiManagerClientCopyInterfaces -> %p\n", (void*)ifaces);
                    if (ifaces) {
                        CFIndex count = CFArrayGetCount(ifaces);
                        printf("MobileWiFi interfaces count: %ld\n", (long)count);
                        for (CFIndex i = 0; i < count; i++) {
                            CFStringRef ifname = (CFStringRef)CFArrayGetValueAtIndex(ifaces, i);
                            char buf[128];
                            if (CFStringGetCString(ifname, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                                printf("  Interface %ld: %s\n", (long)i, buf);
                            }
                        }
                        CFRelease(ifaces);
                    }
                }
                if (WiFiManagerClientCopyDevices) {
                    CFArrayRef devs = WiFiManagerClientCopyDevices(mgr);
                    printf("WiFiManagerClientCopyDevices -> %p\n", (void*)devs);
                    if (devs) {
                        CFIndex count = CFArrayGetCount(devs);
                        printf("MobileWiFi devices count: %ld\n", (long)count);
                        for (CFIndex i = 0; i < count; i++) {
                            void *dev = (void*)CFArrayGetValueAtIndex(devs, i);
                            if (dev && WiFiDeviceClientGetInterfaceName) {
                                CFStringRef ifname = WiFiDeviceClientGetInterfaceName(dev);
                                char buf[128];
                                if (ifname && CFStringGetCString(ifname, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                                    int power = WiFiDeviceClientGetPower ? WiFiDeviceClientGetPower(dev) : -1;
                                    printf("  Device %ld: %s (power: %d)\n", (long)i, buf, power);
                                }
                            }
                        }
                        CFRelease(devs);
                    }
                }
                CFRelease(mgr);
            }
        }
    }

    return 0;
}
