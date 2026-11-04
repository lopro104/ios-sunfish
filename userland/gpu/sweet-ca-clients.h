/*
 * Who HakiosMetalCA shows the device to, on this phone: backboardd and the names in /var/jb/etc/hakiosmetal.clients (one a
 * line), instead of backboardd alone. Other processes get nil: SpringBoard and mediaserverd find pipeline libraries
 * made for Apple's GPUs and abort if shown a device (hakios), so apps are listed one at a time.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <Metal/Metal.h>

static int
sweet_metal_listed(const char *name)
{
	char line[256];
	int yes = 0;
	FILE *f;

	if (name == NULL) {
		return 0;
	}
	if (strcmp(name, "backboardd") == 0) {
		return 1;
	}
	f = fopen("/var/jb/etc/hakiosmetal.clients", "r");
	if (f == NULL) {
		return 0;
	}
	while (!yes && fgets(line, sizeof(line), f) != NULL) {
		line[strcspn(line, "\r\n \t#")] = 0;
		yes = strcmp(line, name) == 0;
	}
	fclose(f);
	return yes;
}

/*
 * Which IOSurfaces a command buffer draws into. At -commit, before the GPU has them: wait (20 ms at most, a stuck hold must
 * not stop the screen) while the display still needs one (IOSurfaceIsInUse: xnu 0182/0194, queued or being copied), since
 * this renderer does not look at that itself and drew into the surface being copied: black bands on the panel for 2-3
 * frames, filmed with sweet-film. Then told to the kernel until completion: kern.sweet_surface_gpu=id / -id (xnu 0186), and
 * the display does not copy such a surface.
 */
enum { SWEET_MARK_CBS = 256, SWEET_MARK_IDS = 8, SWEET_MARK_WAIT_US = 20000 };
static struct { void *cb; IOSurfaceRef surfaces[SWEET_MARK_IDS]; int count; int told; } sweet_marks[SWEET_MARK_CBS];
static pthread_mutex_t sweet_marks_lock = PTHREAD_MUTEX_INITIALIZER;

static void
sweet_surface_tell(int value)
{
	(void)sysctlbyname("kern.sweet_surface_gpu", NULL, NULL, &value, sizeof(value));
}

/*
 * The window server's drawing thread on an A76 (core 7), as the GL layer did with boot-arg glbig: the kernel takes the
 * eight cores as the same and leaves it on the six A55s, where a draw call took 150-290 us against 48 on core 6
 * (metal_fps=draws). kern.sweet_bigcore binds the calling thread.
 */
static void
sweet_big_core(void)
{
	static __thread int bound;

	if (!bound) {
		bound = 1;
		const char *name = getprogname();
		if (name != NULL && strcmp(name, "backboardd") == 0) {
			int core = 7;
			if (sysctlbyname("kern.sweet_bigcore", NULL, NULL, &core, sizeof(core)) != 0) {
				syslog(LOG_ERR, "SweetMarks: kern.sweet_bigcore failed");
			} else {
				syslog(LOG_NOTICE, "SweetMarks: a drawing thread bound to core 7");
			}
		}
	}
}

/* a render pass of this command buffer: its colour attachments' surfaces */
static void
sweet_marks_pass(void *cb, MTLRenderPassDescriptor *descriptor)
{
	sweet_big_core();
	pthread_mutex_lock(&sweet_marks_lock);
	int free_slot = -1, at = -1;
	for (int i = 0; i < SWEET_MARK_CBS && at < 0; i++) {
		if (sweet_marks[i].cb == cb) {
			at = i;
		} else if (sweet_marks[i].cb == NULL && free_slot < 0) {
			free_slot = i;
		}
	}
	for (unsigned i = 0; i < 8; i++) {
		id<MTLTexture> t = descriptor.colorAttachments[i].texture;
		IOSurfaceRef surface = t != nil ? t.iosurface : NULL;
		if (surface == NULL) {
			continue;
		}
		if (at < 0) {
			if (free_slot < 0) {
				break;
			}
			at = free_slot;
			sweet_marks[at].cb = cb;
			sweet_marks[at].count = 0;
			sweet_marks[at].told = 0;
		}
		int seen = 0;
		for (int j = 0; j < sweet_marks[at].count; j++) {
			seen = seen || sweet_marks[at].surfaces[j] == surface;
		}
		if (!seen && sweet_marks[at].count < SWEET_MARK_IDS) {
			CFRetain(surface);
			sweet_marks[at].surfaces[sweet_marks[at].count++] = surface;
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);
}

static void
sweet_marks_commit(void *cb)
{
	IOSurfaceRef surfaces[SWEET_MARK_IDS];
	int count = 0;

	pthread_mutex_lock(&sweet_marks_lock);
	for (int i = 0; i < SWEET_MARK_CBS; i++) {
		if (sweet_marks[i].cb == cb) {
			count = sweet_marks[i].count;
			for (int j = 0; j < count; j++) {
				surfaces[j] = sweet_marks[i].surfaces[j];
			}
			sweet_marks[i].told = 1;
			break;
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);

	/* (the slot keeps them retained: it is freed at completion, which comes after this) */
	static unsigned commits, waited, timed_out;
	static uint64_t waited_us;
	unsigned us = 0;
	for (int j = 0; j < count; j++) {
		while (IOSurfaceIsInUse(surfaces[j]) && us < SWEET_MARK_WAIT_US) {
			usleep(250);
			us += 250;
		}
		sweet_surface_tell((int)IOSurfaceGetID(surfaces[j]));
	}
	if (count > 0) {
		commits++;
		waited += us != 0;
		timed_out += us >= SWEET_MARK_WAIT_US;
		waited_us += us;
		/* Diagnostic syslog removed when settled to eliminate commit lock/log overhead */
	}
}

static void
sweet_marks_done(void *cb)
{
	pthread_mutex_lock(&sweet_marks_lock);
	for (int i = 0; i < SWEET_MARK_CBS; i++) {
		if (sweet_marks[i].cb == cb) {
			for (int j = 0; j < sweet_marks[i].count; j++) {
				if (sweet_marks[i].told) {
					sweet_surface_tell(-(int)IOSurfaceGetID(sweet_marks[i].surfaces[j]));
				}
				CFRelease(sweet_marks[i].surfaces[j]);
			}
			sweet_marks[i].cb = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);
}
