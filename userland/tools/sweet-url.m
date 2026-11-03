/*
 * sweet-url: fetch URLs with NSURLSession, as apps do, and say what came back and how long it took.
 *
 *   sweet-url URL [URL...]
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
	NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
	config.timeoutIntervalForRequest = 15;
	NSURLSession *session = [NSURLSession sessionWithConfiguration:config];
	for (int i = 1; i < argc; i++) {
		dispatch_semaphore_t done = dispatch_semaphore_create(0);
		NSDate *began = [NSDate date];
		NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:argv[i]]];
		[[session dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			printf("%s: %.2f s, ", argv[i], -[began timeIntervalSinceNow]);
			if (error != nil) {
				printf("error %ld %s\n", (long)error.code, error.localizedDescription.UTF8String);
			} else {
				printf("HTTP %ld, %lu bytes\n", (long)((NSHTTPURLResponse *)response).statusCode, (unsigned long)data.length);
			}
			dispatch_semaphore_signal(done);
		}] resume];
		dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
	}
	return 0;
}
