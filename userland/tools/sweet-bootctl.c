/*
 * sweet-bootctl: Qualcomm A/B boot control CLI for iOS on Snapdragon (SM7150 / sweet & sunfish).
 *
 * Implements Android bootctl command line compatibility using XNU sysctls:
 *   kern.sweet_boot_successful (RW)
 *   kern.sweet_boot_slot (RW)
 *   kern.sweet_bootctl (RW)
 *
 * Commands:
 *   hal-info
 *   get-number-slots
 *   get-current-slot
 *   mark-boot-successful
 *   is-slot-marked-successful [SLOT]
 *   is-slot-bootable [SLOT]
 *   get-suffix [SLOT]
 *   set-active-boot-slot [SLOT]
 *   set-slot-as-unbootable [SLOT]
 *   status
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/sysctl.h>

static void
usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s COMMAND [ARGS...]\n\n"
	    "Commands:\n"
	    "  hal-info                     - Print HAL information\n"
	    "  get-number-slots             - Print total number of slots (2)\n"
	    "  get-current-slot             - Print current slot (0 or 1)\n"
	    "  mark-boot-successful         - Mark current slot as successfully booted\n"
	    "  is-slot-marked-successful [S]- Print 1 if slot S (default current) is marked successful, 0 otherwise\n"
	    "  is-slot-bootable [SLOT]      - Print 1 if slot S (default current) is bootable, 0 otherwise\n"
	    "  get-suffix [SLOT]            - Print slot suffix (_a for 0, _b for 1)\n"
	    "  set-active-boot-slot [SLOT]  - Set active boot slot\n"
	    "  set-slot-as-unbootable [SLOT]- Set slot as unbootable\n"
	    "  status                       - Print full slot status from kernel\n",
	    prog);
}

static int
parse_slot(const char *arg)
{
	if (!arg) return -1;
	if (strcmp(arg, "0") == 0 || strcmp(arg, "a") == 0 || strcmp(arg, "_a") == 0) return 0;
	if (strcmp(arg, "1") == 0 || strcmp(arg, "b") == 0 || strcmp(arg, "_b") == 0) return 1;
	return -1;
}

static int
get_current_slot(void)
{
	int slot = 0;
	size_t len = sizeof(slot);
	if (sysctlbyname("kern.sweet_boot_slot", &slot, &len, NULL, 0) != 0) {
		perror("sysctlbyname kern.sweet_boot_slot");
		return -1;
	}
	return slot;
}

struct slot_status {
	int cur_slot;
	int s0_good;
	int s0_boot;
	int s1_good;
	int s1_boot;
};

static int
get_slot_status(struct slot_status *st)
{
	char buf[256];
	size_t len = sizeof(buf) - 1;
	memset(buf, 0, sizeof(buf));
	if (sysctlbyname("kern.sweet_bootctl", buf, &len, NULL, 0) != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "bootctl: kernel does not support kern.sweet_bootctl\n");
		} else {
			perror("sysctlbyname kern.sweet_bootctl");
		}
		return -1;
	}
	buf[len] = '\0';
	/* Format: "slot=%d (%s) slot0: good=%d bootable=%d slot1: good=%d bootable=%d\n" */
	char suffix[16];
	int matched = sscanf(buf, "slot=%d (%15[^)]) slot0: good=%d bootable=%d slot1: good=%d bootable=%d",
	    &st->cur_slot, suffix, &st->s0_good, &st->s0_boot, &st->s1_good, &st->s1_boot);
	if (matched < 5) {
		fprintf(stderr, "bootctl: failed to parse status string: %s\n", buf);
		return -1;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[1];

	if (strcmp(cmd, "hal-info") == 0) {
		printf("HAL Version: Qualcomm SM7150 / Snapdragon A/B boot control (XNU)\n");
		return 0;
	}

	if (strcmp(cmd, "get-number-slots") == 0) {
		printf("2\n");
		return 0;
	}

	if (strcmp(cmd, "get-current-slot") == 0) {
		int slot = get_current_slot();
		if (slot < 0) return 1;
		printf("%d\n", slot);
		return 0;
	}

	if (strcmp(cmd, "mark-boot-successful") == 0) {
		int val = 1;
		if (sysctlbyname("kern.sweet_boot_successful", NULL, NULL, &val, sizeof(val)) != 0) {
			perror("sysctlbyname kern.sweet_boot_successful");
			return 1;
		}
		return 0;
	}

	if (strcmp(cmd, "is-slot-marked-successful") == 0) {
		struct slot_status st;
		if (get_slot_status(&st) != 0) return 1;
		int slot = st.cur_slot;
		if (argc >= 3) {
			slot = parse_slot(argv[2]);
			if (slot < 0) {
				fprintf(stderr, "bootctl: invalid slot: %s\n", argv[2]);
				return 1;
			}
		}
		printf("%d\n", (slot == 0) ? st.s0_good : st.s1_good);
		return 0;
	}

	if (strcmp(cmd, "is-slot-bootable") == 0) {
		struct slot_status st;
		if (get_slot_status(&st) != 0) return 1;
		int slot = st.cur_slot;
		if (argc >= 3) {
			slot = parse_slot(argv[2]);
			if (slot < 0) {
				fprintf(stderr, "bootctl: invalid slot: %s\n", argv[2]);
				return 1;
			}
		}
		printf("%d\n", (slot == 0) ? st.s0_boot : st.s1_boot);
		return 0;
	}

	if (strcmp(cmd, "get-suffix") == 0) {
		int slot = 0;
		if (argc >= 3) {
			slot = parse_slot(argv[2]);
			if (slot < 0) {
				fprintf(stderr, "bootctl: invalid slot: %s\n", argv[2]);
				return 1;
			}
		} else {
			slot = get_current_slot();
			if (slot < 0) return 1;
		}
		printf("%s\n", (slot == 0) ? "_a" : "_b");
		return 0;
	}

	if (strcmp(cmd, "set-active-boot-slot") == 0) {
		if (argc < 3) {
			fprintf(stderr, "bootctl: missing slot argument\n");
			return 1;
		}
		int slot = parse_slot(argv[2]);
		if (slot < 0) {
			fprintf(stderr, "bootctl: invalid slot: %s\n", argv[2]);
			return 1;
		}
		if (sysctlbyname("kern.sweet_boot_slot", NULL, NULL, &slot, sizeof(slot)) != 0) {
			perror("sysctlbyname kern.sweet_boot_slot");
			return 1;
		}
		return 0;
	}

	if (strcmp(cmd, "set-slot-as-unbootable") == 0) {
		if (argc < 3) {
			fprintf(stderr, "bootctl: missing slot argument\n");
			return 1;
		}
		int slot = parse_slot(argv[2]);
		if (slot < 0) {
			fprintf(stderr, "bootctl: invalid slot: %s\n", argv[2]);
			return 1;
		}
		char cmd_str[32];
		snprintf(cmd_str, sizeof(cmd_str), "set-unbootable %d", slot);
		if (sysctlbyname("kern.sweet_bootctl", NULL, NULL, cmd_str, strlen(cmd_str)) != 0) {
			perror("sysctlbyname kern.sweet_bootctl");
			return 1;
		}
		return 0;
	}

	if (strcmp(cmd, "status") == 0) {
		char buf[256];
		size_t len = sizeof(buf) - 1;
		memset(buf, 0, sizeof(buf));
		if (sysctlbyname("kern.sweet_bootctl", buf, &len, NULL, 0) != 0) {
			perror("sysctlbyname kern.sweet_bootctl");
			return 1;
		}
		printf("%s", buf);
		return 0;
	}

	if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "help") == 0) {
		usage(argv[0]);
		return 0;
	}

	fprintf(stderr, "bootctl: unknown command: %s\n", cmd);
	usage(argv[0]);
	return 1;
}
