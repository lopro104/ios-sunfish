/*
 * Does Mesa reach the Adreno? An OSMesa context (freedreno through the kernel's SweetGPU service
 * when there is one, softpipe on the CPU with OSMESA_SOFTWARE=1), a clear and a triangle into a
 * 64x64 buffer, read back. Everything it finds goes to /dev/console, which is in the persistent
 * log (tools/sweet-log-pull.sh), as lines starting with "SWEET GPUTEST:".
 */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <GL/osmesa.h>
#include <GL/gl.h>

#define SIZE 64

static FILE *out;

static void
say(const char *format, ...)
{
	va_list ap;
	fputs("SWEET GPUTEST: ", out);
	va_start(ap, format);
	vfprintf(out, format, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);
}

static unsigned
pixel(const unsigned char *buffer, int x, int y)
{
	const unsigned char *p = buffer + 4 * (y * SIZE + x);
	return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

int
main(void)
{
	int console = open("/dev/console", O_WRONLY);
	out = console >= 0 ? fdopen(console, "w") : stdout;

	unsigned char *buffer = calloc(SIZE * SIZE, 4);
	OSMesaContext context = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
	if (buffer == NULL || context == NULL) {
		say("no context (OSMesaCreateContextExt)");
		return 1;
	}
	if (!OSMesaMakeCurrent(context, buffer, GL_UNSIGNED_BYTE, SIZE, SIZE)) {
		say("OSMesaMakeCurrent failed");
		return 1;
	}
	say("renderer \"%s\", vendor \"%s\", version \"%s\"", (const char *)glGetString(GL_RENDERER),
	    (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_VERSION));

	/* A blue background and a red triangle over the middle: drawn, not only cleared. */
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBegin(GL_TRIANGLES);
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex2f(-0.8f, -0.8f);
	glVertex2f(0.8f, -0.8f);
	glVertex2f(0.0f, 0.8f);
	glEnd();
	glFinish();
	GLenum error = glGetError();

	unsigned corner = pixel(buffer, 1, 1), middle = pixel(buffer, SIZE / 2, SIZE / 2);
	say("gl error 0x%x; corner 0x%08x (want 0x0000ffff, blue), middle 0x%08x (want 0xff0000ff, red)",
	    error, corner, middle);
	if (corner == 0x0000ffffu && middle == 0xff0000ffu) {
		say("THE PIXELS ARE RIGHT");
	} else {
		say("the pixels are wrong");
	}
	OSMesaDestroyContext(context);
	return 0;
}
