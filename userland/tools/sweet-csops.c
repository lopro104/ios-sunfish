/*
 * sweet-csops: what the kernel answers about a process's code signature (csops, sys/codesign.h).
 *
 *   sweet-csops PID [PID...]
 *
 * Read only: CS_OPS_STATUS flags, and the sizes the kernel gives for the entitlements blob (7) and the DER
 * entitlements blob (16), or the errno when it refuses.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

static void
blob(pid_t pid, unsigned ops, const char *name)
{
	static uint8_t buf[256 * 1024];
	memset(buf, 0, 8);
	int r = csops(pid, ops, buf, sizeof(buf));
	uint32_t len = (uint32_t)buf[4] << 24 | (uint32_t)buf[5] << 16 | (uint32_t)buf[6] << 8 | buf[7];
	printf("  %s: %s", name, r == 0 ? "ok" : strerror(errno));
	if (r == 0) {
		printf(", magic %02x%02x%02x%02x, %u bytes", buf[0], buf[1], buf[2], buf[3], len);
	}
	printf("\n");
}

int
main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		pid_t pid = (pid_t)atoi(argv[i]);
		uint32_t flags = 0;
		int r = csops(pid, 0, &flags, sizeof(flags));
		printf("pid %d: status %s, flags 0x%08x\n", pid, r == 0 ? "ok" : strerror(errno), flags);
		blob(pid, 7, "entitlements blob");
		blob(pid, 16, "DER entitlements blob");
	}
	return 0;
}
