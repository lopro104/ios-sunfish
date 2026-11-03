/*
 * Who HakiosMetalCA shows the device to, on this phone: backboardd and the names in /var/jb/etc/hakiosmetal.clients (one a
 * line), instead of backboardd alone. Other processes get nil: SpringBoard and mediaserverd find pipeline libraries
 * made for Apple's GPUs and abort if shown a device (hakios), so apps are listed one at a time.
 */
#include <stdio.h>
#include <string.h>

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
