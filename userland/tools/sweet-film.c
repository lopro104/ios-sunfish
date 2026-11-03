/*
 * sweet-film: a quarter-size film of IOSurfaces, for glitches too short for sweet-shot.
 *
 *   sweet-film FILE FRAMES INTERVAL_MS ID [ID...]
 *
 * Every INTERVAL_MS, each listed surface (e.g. 1 = the panel's memory, 2 and 3 = what backboardd swaps
 * onto it) is sampled at every 4th pixel of every 4th row, unlocked (a read of what is there, as the
 * display copy reads it). FILE: "SWFILM" u32 frames, surfaces, then per frame and surface: u32 id,
 * width/4, height/4, u64 host time, then BGRX rows. tools/sweet-film.py makes PNG sheets of it.
 * SWEET_FILM_STEP=N samples every Nth pixel and row instead of every 4th; SWEET_FILM_MB=N holds up to N MiB in memory
 * and writes them at the end, for a rate the disk could not keep up with.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <IOSurface/IOSurfaceRef.h>

#define MAX_SURFACES 4

int
main(int argc, char *argv[])
{
	if (argc < 5) {
		fprintf(stderr, "usage: sweet-film FILE FRAMES INTERVAL_MS ID [ID...]\n");
		return 2;
	}
	uint32_t frames = (uint32_t)atoi(argv[2]), interval = (uint32_t)atoi(argv[3]), count = 0;
	IOSurfaceRef surfaces[MAX_SURFACES];
	uint32_t ids[MAX_SURFACES];
	for (int i = 4; i < argc && count < MAX_SURFACES; i++) {
		ids[count] = (uint32_t)strtoul(argv[i], NULL, 0);
		surfaces[count] = IOSurfaceLookup(ids[count]);
		if (surfaces[count] == NULL) {
			fprintf(stderr, "sweet-film: no surface %u\n", ids[count]);
			return 1;
		}
		count++;
	}
	FILE *f = fopen(argv[1], "wb");
	if (f == NULL) {
		perror(argv[1]);
		return 1;
	}
	uint32_t step = getenv("SWEET_FILM_STEP") ? (uint32_t)atoi(getenv("SWEET_FILM_STEP")) : 4;
	if (getenv("SWEET_FILM_MB")) {
		setvbuf(f, malloc((size_t)atoi(getenv("SWEET_FILM_MB")) << 20), _IOFBF, (size_t)atoi(getenv("SWEET_FILM_MB")) << 20);
	}
	fwrite("SWFILM", 1, 6, f);
	fwrite(&frames, 4, 1, f);
	fwrite(&count, 4, 1, f);
	uint32_t *line = malloc(4096 * 4);
	for (uint32_t n = 0; n < frames; n++) {
		for (uint32_t k = 0; k < count; k++) {
			IOSurfaceRef s = surfaces[k];
			uint32_t w = (uint32_t)IOSurfaceGetWidth(s) / step, h = (uint32_t)IOSurfaceGetHeight(s) / step;
			size_t row = IOSurfaceGetBytesPerRow(s);
			const uint8_t *base = IOSurfaceGetBaseAddress(s);
			uint64_t t = mach_absolute_time();
			uint32_t head[3] = { ids[k], w, h };
			fwrite(head, sizeof(head), 1, f);
			fwrite(&t, 8, 1, f);
			for (uint32_t y = 0; y < h; y++) {
				const uint32_t *src = (const uint32_t *)(base + (size_t)y * step * row);
				for (uint32_t x = 0; x < w; x++) {
					line[x] = src[x * step];
				}
				fwrite(line, 4, w, f);
			}
		}
		usleep(interval * 1000);
	}
	fclose(f);
	printf("sweet-film: %u frames of %u surfaces -> %s\n", frames, count, argv[1]);
	return 0;
}
