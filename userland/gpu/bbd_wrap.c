/*
 * /usr/libexec/backboardd: launchd does not pass DYLD_* variables from a job to a system daemon,
 * so this stands in for the window server: it adds our loader to DYLD_INSERT_LIBRARIES and execs
 * the real one (/usr/libexec/real/backboardd: same pid, same Mach services launchd prepared).
 * From ios-topaz userland/gpu/bbd_wrap.c (their env file left out). Without the loader, or with
 * boot-arg sweetgl=0, backboardd starts as it always did.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>

#define REAL "/usr/libexec/real/backboardd"
#define LOADER "/usr/lib/libsunfish-inject.dylib"

int
main(int argc, char *argv[])
{
	(void)argc;
	char bootargs[1024] = "";
	size_t blen = sizeof(bootargs) - 1;
	(void)sysctlbyname("kern.bootargs", bootargs, &blen, NULL, 0);
	if (access(LOADER, R_OK) == 0 && strstr(bootargs, "sweetgl=0") == NULL) {
		setenv("DYLD_INSERT_LIBRARIES", LOADER, 1);
	}
	argv[0] = (char *)REAL;
	execv(REAL, argv);
	return 127;
}
