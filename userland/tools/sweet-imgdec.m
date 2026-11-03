/*
 * sweet-imgdec: an image file decoded on the phone by ImageIO, into memory by CoreGraphics, no GPU involved.
 *
 *   sweet-imgdec IN OUT       OUT: "SWIMG" u32 width, height, then RGBA rows
 *
 * For telling a decoder's fault from a renderer's: compare OUT with the same file decoded on the PC.
 */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: sweet-imgdec IN OUT\n");
		return 2;
	}
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
	CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
	CGImageRef image = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
	if (image == NULL) {
		fprintf(stderr, "sweet-imgdec: no image from %s\n", argv[1]);
		return 1;
	}
	uint32_t size[2] = { (uint32_t)CGImageGetWidth(image), (uint32_t)CGImageGetHeight(image) };
	uint8_t *pixels = calloc(size[1], size[0] * 4);
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c = CGBitmapContextCreate(pixels, size[0], size[1], 8, size[0] * 4, rgb, kCGImageAlphaPremultipliedLast);
	CGContextDrawImage(c, CGRectMake(0, 0, size[0], size[1]), image);
	FILE *f = fopen(argv[2], "wb");
	fwrite("SWIMG", 1, 5, f);
	fwrite(size, 4, 2, f);
	fwrite(pixels, 4, (size_t)size[0] * size[1], f);
	fclose(f);
	printf("%s: %u x %u, bits %zu, type %s\n", argv[1], size[0], size[1], CGImageGetBitsPerComponent(image),
	    CFStringGetCStringPtr(CGImageSourceGetType(src), kCFStringEncodingUTF8));
	return 0;
}
