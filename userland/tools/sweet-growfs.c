/*
 * sweet-growfs: grow the mounted HFS+ root to the end of userdata, short of the kernel's log copy.
 *
 *   sweet-growfs          print the sizes and the target, change nothing
 *   sweet-growfs --grow   then grow / to the target (HFSIOC_RESIZE_VOLUME -> hfs_extendfs)
 *
 * sweet_ufs reports the whole userdata partition (DKIOCGETBLOCKCOUNT), but its last 4 MiB + 4 KiB
 * hold the persistent log ring (SWEET_LOG_BLOCKS in bsd/dev/sweet_ufs.c), and hfs_extendfs only
 * refuses sizes past the device's end. The target is the device size minus that, rounded down to
 * 1 MiB. Back the root up first: the resize code had never run on this kernel.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

/* sys/disk.h and sys/fsctl.h are not in the iOS SDK */
#define DKIOCGETBLOCKSIZE       _IOR('d', 24, uint32_t)
#define DKIOCGETBLOCKCOUNT      _IOR('d', 25, uint64_t)
#define HFSIOC_RESIZE_VOLUME    _IOW('h', 2, u_int64_t)
#define LOG_RESERVE             ((4ull << 20) + 4096)
#define MIB                     (1ull << 20)

int fsctl(const char *, unsigned long, void *, unsigned int);

int
main(int argc, char *argv[])
{
	int grow = argc > 1 && strcmp(argv[1], "--grow") == 0;
	uint32_t secsize = 0;
	uint64_t count = 0;
	struct statfs sf;

	if (argc > 1 && !grow) {
		fprintf(stderr, "usage: sweet-growfs [--grow]\n");
		return 2;
	}
	if (statfs("/", &sf) != 0) {
		perror("statfs /");
		return 1;
	}
	if (strcmp(sf.f_fstypename, "hfs") != 0 || strcmp(sf.f_mntfromname, "/dev/ufsroot0") != 0) {
		fprintf(stderr, "sweet-growfs: / is %s on %s, not hfs on /dev/ufsroot0\n", sf.f_fstypename, sf.f_mntfromname);
		return 1;
	}
	/* the raw node: the block one is refused while mounted */
	int fd = open("/dev/rufsroot0", O_RDONLY);
	if (fd < 0 || ioctl(fd, DKIOCGETBLOCKSIZE, &secsize) != 0 || ioctl(fd, DKIOCGETBLOCKCOUNT, &count) != 0) {
		perror("/dev/rufsroot0");
		return 1;
	}
	close(fd);
	uint64_t device = count * secsize, current = (uint64_t)sf.f_blocks * sf.f_bsize;
	uint64_t target = (device - LOG_RESERVE) / MIB * MIB;
	printf("device:  %llu bytes (%llu sectors of %u), %llu MiB\n", device, count, secsize, device / MIB);
	printf("root:    %llu bytes, %llu MiB (blocks of %u)\n", current, current / MIB, (unsigned)sf.f_bsize);
	printf("target:  %llu bytes, %llu MiB (log ring left: %llu bytes at the end)\n", target, target / MIB, device - target);
	if (target <= current || target % sf.f_bsize != 0) {
		fprintf(stderr, "sweet-growfs: nothing to grow (or target not a multiple of the block size)\n");
		return target <= current ? 0 : 1;
	}
	if (!grow) {
		printf("dry run; --grow to resize\n");
		return 0;
	}
	printf("growing / ...\n");
	fflush(stdout);
	sync();
	if (fsctl("/", HFSIOC_RESIZE_VOLUME, &target, 0) != 0) {
		fprintf(stderr, "sweet-growfs: HFSIOC_RESIZE_VOLUME: %s\n", strerror(errno));
		return 1;
	}
	sync();
	if (statfs("/", &sf) == 0) {
		printf("root now: %llu MiB\n", (uint64_t)sf.f_blocks * sf.f_bsize / MIB);
	}
	return 0;
}
