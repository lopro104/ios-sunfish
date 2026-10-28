/*
 * sweet-objc-methods: list a class's methods as this iOS has them (to hook them by name).
 *
 *   sweet-objc-methods FRAMEWORK-PATH CLASS
 */
#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

static void
list(Class cls, char kind)
{
	unsigned int n = 0;
	Method *m = class_copyMethodList(cls, &n);

	for (unsigned int i = 0; i < n; i++) {
		printf("%c[%s %s] %s\n", kind, class_getName(cls), sel_getName(method_getName(m[i])),
		    method_getTypeEncoding(m[i]));
	}
	free(m);
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s FRAMEWORK-PATH CLASS\n", argv[0]);
		return 2;
	}
	if (dlopen(argv[1], RTLD_NOW) == NULL) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 1;
	}
	Class cls = objc_getClass(argv[2]);
	if (cls == NULL) {
		fprintf(stderr, "no class %s\n", argv[2]);
		return 1;
	}
	list(object_getClass((id)cls), '+');
	list(cls, '-');
	return 0;
}
