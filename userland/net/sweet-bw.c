/*
 * sweet-bw HOST PORT rx|tx [SECONDS]: TCP throughput test against a plain server (the phone has no curl or nc).
 * rx: connects and reads for SECONDS; tx: connects and writes. Prints Mbit/s. Pair with the server in tools/sweet-bw-server.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static double
now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

int
main(int argc, char **argv)
{
	static char buf[65536];
	struct sockaddr_in sa;
	double t0, t1, secs = argc > 4 ? atof(argv[4]) : 10;
	unsigned long long total = 0;
	int s, tx;

	if (argc < 4) {
		fprintf(stderr, "usage: sweet-bw HOST PORT rx|tx [SECONDS]\n");
		return 2;
	}
	tx = strcmp(argv[3], "tx") == 0;
	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(argv[2]));
	inet_pton(AF_INET, argv[1], &sa.sin_addr);
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("connect");
		return 1;
	}
	memset(buf, 'x', sizeof(buf));
	t0 = now();
	while ((t1 = now()) - t0 < secs) {
		ssize_t n = tx ? write(s, buf, sizeof(buf)) : read(s, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		total += (unsigned long long)n;
	}
	printf("%s: %llu bytes in %.1f s = %.1f Mbit/s\n", tx ? "tx" : "rx", total, t1 - t0, total * 8 / (t1 - t0) / 1e6);
	return 0;
}
