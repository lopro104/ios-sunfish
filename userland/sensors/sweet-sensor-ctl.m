/*
 * sweet-sensor-ctl: Userland control tool and daemon for sensors, auto-rotation, brightness, and proximity.
 */

#import <Foundation/Foundation.h>
#include <notify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

static void
set_hw_brightness(int val)
{
    if (val < 0) val = 0;
    if (val > 2047) val = 2047;
    int cur = 0;
    size_t sz = sizeof(cur);
    if (sysctlbyname("kern.sweet_brightness", &cur, &sz, &val, sizeof(val)) == 0) {
        printf("sweet-sensord: set kern.sweet_brightness=%d\n", val);
    }
}

static void
run_daemon(void)
{
    printf("sweet-sensord: starting sensor helper daemon (PID %d)...\n", getpid());
    int token = 0;

    // Proximity
    notify_register_dispatch("com.sweet.prox.near", &token, dispatch_get_main_queue(), ^(__unused int t) {
        printf("sweet-sensord: proximity NEAR -> blanking hardware brightness\n");
        set_hw_brightness(0);
    });
    notify_register_dispatch("com.sweet.prox.far", &token, dispatch_get_main_queue(), ^(__unused int t) {
        printf("sweet-sensord: proximity FAR -> restoring hardware brightness (500)\n");
        set_hw_brightness(500);
    });

    // Brightness state sync
    notify_register_dispatch("com.sweet.brightness", &token, dispatch_get_main_queue(), ^(int t) {
        uint64_t state = 500;
        notify_get_state(t, &state);
        printf("sweet-sensord: received com.sweet.brightness -> %llu\n", (unsigned long long)state);
        set_hw_brightness((int)state);
    });

    printf("sweet-sensord: running runloop.\n");
    CFRunLoopRun();
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 2) {
            printf("Usage: sweet-sensor-ctl <command> [args...]\n\n");
            printf("Commands:\n");
            printf("  status                 - Query orientation, brightness, and display status\n");
            printf("  rotate <portrait|landscape|left|upside_down> - Rotate screen\n");
            printf("  brightness <0..2047>   - Set physical hardware panel brightness\n");
            printf("  prox <near|far>        - Trigger proximity sensor event (pocket/ear mode)\n");
            printf("  wake                   - Wake and unblank the display\n");
            printf("  unlock                 - Wake and unlock device\n");
            printf("  daemon                 - Run background helper daemon for brightness/prox\n");
            return 0;
        }

        const char *cmd = argv[1];

        if (strcmp(cmd, "status") == 0) {
            int hw_brightness = -1;
            size_t sz = sizeof(hw_brightness);
            sysctlbyname("kern.sweet_brightness", &hw_brightness, &sz, NULL, 0);

            int shown = 0;
            sz = sizeof(shown);
            sysctlbyname("kern.sweetfb_shown", &shown, &sz, NULL, 0);

            printf("=== Sweet Sensor & Display Status ===\n");
            printf("Hardware Brightness (DCS 0x51): %d / 2047\n", hw_brightness);
            printf("Frames Shown by Kernel:         %d\n", shown);
        } else if (strcmp(cmd, "rotate") == 0) {
            if (argc < 3) {
                printf("Usage: sweet-sensor-ctl rotate <portrait|landscape|left|upside_down>\n");
                return 1;
            }
            const char *arg = argv[2];
            const char *notif = "com.sweet.rotate.portrait";
            if (strcmp(arg, "portrait") == 0 || strcmp(arg, "1") == 0) {
                notif = "com.sweet.rotate.portrait";
            } else if (strcmp(arg, "landscape") == 0 || strcmp(arg, "right") == 0 || strcmp(arg, "3") == 0) {
                notif = "com.sweet.rotate.landscape";
            } else if (strcmp(arg, "left") == 0 || strcmp(arg, "4") == 0) {
                notif = "com.sweet.rotate.landscape_left";
            } else if (strcmp(arg, "upside_down") == 0 || strcmp(arg, "2") == 0) {
                notif = "com.sweet.rotate.upside_down";
            }
            printf("Posting rotation request (%s -> %s)...\n", arg, notif);
            notify_post(notif);
            printf("Rotation event posted.\n");
        } else if (strcmp(cmd, "brightness") == 0) {
            if (argc < 3) {
                printf("Usage: sweet-sensor-ctl brightness <0..2047>\n");
                return 1;
            }
            int val = atoi(argv[2]);
            set_hw_brightness(val);
        } else if (strcmp(cmd, "prox") == 0) {
            if (argc < 3) {
                printf("Usage: sweet-sensor-ctl prox <near|far>\n");
                return 1;
            }
            const char *notif = (strcmp(argv[2], "near") == 0 || strcmp(argv[2], "1") == 0) ?
                "com.sweet.prox.near" : "com.sweet.prox.far";
            printf("Posting proximity event: %s (%s)...\n", argv[2], notif);
            notify_post(notif);
            printf("Proximity event posted.\n");
        } else if (strcmp(cmd, "wake") == 0) {
            printf("Posting wake event...\n");
            notify_post("com.sweet.wake");
            printf("Wake event posted.\n");
        } else if (strcmp(cmd, "unlock") == 0) {
            printf("Posting unlock event...\n");
            notify_post("com.sweet.unlock");
            printf("Unlock requested.\n");
        } else if (strcmp(cmd, "daemon") == 0) {
            run_daemon();
        } else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            return 1;
        }
    }
    return 0;
}
