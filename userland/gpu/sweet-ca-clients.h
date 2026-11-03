/*
 * Who HakiosMetalCA shows the device to, on this phone: backboardd and the names in /var/jb/etc/hakiosmetal.clients (one a
 * line), instead of backboardd alone. Other processes get nil: SpringBoard and mediaserverd find pipeline libraries
 * made for Apple's GPUs and abort if shown a device (hakios), so apps are listed one at a time.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/sysctl.h>
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
 * Which IOSurfaces a command buffer draws into, told to the kernel from -commit (before CoreAnimation's swap) until its
 * completion: kern.sweet_surface_gpu=id / -id (xnu 0186), and the display does not copy such a surface. Without it the
 * panel shows frames the GPU is still drawing: patches of the screen black for 2-3 frames in an animation.
 */
enum { SWEET_MARK_CBS = 256, SWEET_MARK_IDS = 8 };
static struct { void *cb; int ids[SWEET_MARK_IDS]; int count; int told; } sweet_marks[SWEET_MARK_CBS];
static pthread_mutex_t sweet_marks_lock = PTHREAD_MUTEX_INITIALIZER;

static void
sweet_surface_tell(int value)
{
	(void)sysctlbyname("kern.sweet_surface_gpu", NULL, NULL, &value, sizeof(value));
}

/* a render pass of this command buffer: its colour attachments' surfaces */
static void
sweet_marks_pass(void *cb, MTLRenderPassDescriptor *descriptor)
{
	int ids[SWEET_MARK_IDS], n = 0;

	for (unsigned i = 0; i < 8 && n < SWEET_MARK_IDS; i++) {
		id<MTLTexture> t = descriptor.colorAttachments[i].texture;
		IOSurfaceRef surface = t != nil ? t.iosurface : NULL;
		if (surface != NULL) {
			ids[n++] = (int)IOSurfaceGetID(surface);
		}
	}
	if (n == 0) {
		return;
	}
	pthread_mutex_lock(&sweet_marks_lock);
	int free_slot = -1, at = -1;
	for (int i = 0; i < SWEET_MARK_CBS && at < 0; i++) {
		if (sweet_marks[i].cb == cb) {
			at = i;
		} else if (sweet_marks[i].cb == NULL && free_slot < 0) {
			free_slot = i;
		}
	}
	if (at < 0 && free_slot >= 0) {
		at = free_slot;
		sweet_marks[at].cb = cb;
		sweet_marks[at].count = 0;
		sweet_marks[at].told = 0;
	}
	for (int k = 0; at >= 0 && k < n; k++) {
		int seen = 0;
		for (int j = 0; j < sweet_marks[at].count; j++) {
			seen = seen || sweet_marks[at].ids[j] == ids[k];
		}
		if (!seen && sweet_marks[at].count < SWEET_MARK_IDS) {
			sweet_marks[at].ids[sweet_marks[at].count++] = ids[k];
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);
}

static void
sweet_marks_commit(void *cb)
{
	pthread_mutex_lock(&sweet_marks_lock);
	for (int i = 0; i < SWEET_MARK_CBS; i++) {
		if (sweet_marks[i].cb == cb) {
			sweet_marks[i].told = 1;
			for (int j = 0; j < sweet_marks[i].count; j++) {
				sweet_surface_tell(sweet_marks[i].ids[j]);
			}
			break;
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);
}

static void
sweet_marks_done(void *cb)
{
	pthread_mutex_lock(&sweet_marks_lock);
	for (int i = 0; i < SWEET_MARK_CBS; i++) {
		if (sweet_marks[i].cb == cb) {
			for (int j = 0; sweet_marks[i].told && j < sweet_marks[i].count; j++) {
				sweet_surface_tell(-sweet_marks[i].ids[j]);
			}
			sweet_marks[i].cb = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&sweet_marks_lock);
}
