/*
 * sweet-autoconf6 IFNAME [stop]: start (or stop) IPv6 on an interface the way IPConfiguration does for an "IPv6 Automatic" service:
 * SIOCLL_START attaches IPv6 and configures the link-local address, SIOCAUTOCONF_START sets IFEF_ACCEPT_RTADVD and sends a router
 * solicitation. ew0 is not a configd-managed service, so nothing else does it (without the link-local step the interface has no IPv6
 * at all and the router advertisement never gets answered). The ioctl numbers encode the struct sizes: in6_ifreq is 288 bytes on
 * Darwin (16 byte name + the 272 byte icmp6_ifstat member of the union), in6_aliasreq 128; declared here because the SDK has no
 * netinet6/in6_var.h.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

struct in6_ifreq_x {
	char ifr_name[16];
	uint8_t ifr_ifru[272];
};

struct in6_aliasreq_x {
	char ifra_name[16];
	uint8_t rest[112];                      /* addr, dstaddr, prefixmask (3 x sockaddr_in6), flags, lifetime: zero = kernel picks the address */
};

#define SIOCLL_START_X       _IOWR('i', 130, struct in6_aliasreq_x)
#define SIOCLL_STOP_X        _IOWR('i', 131, struct in6_aliasreq_x)
#define SIOCAUTOCONF_START_X _IOWR('i', 132, struct in6_ifreq_x)
#define SIOCAUTOCONF_STOP_X  _IOWR('i', 133, struct in6_ifreq_x)

int
main(int argc, char **argv)
{
	struct in6_ifreq_x ifr;
	int s, stop = argc > 2 && strcmp(argv[2], "stop") == 0;

	if (argc < 2) {
		fprintf(stderr, "usage: sweet-autoconf6 IFNAME [stop]\n");
		return 2;
	}
	s = socket(AF_INET6, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket");
		return 1;
	}
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
	{
		struct in6_aliasreq_x ar;

		memset(&ar, 0, sizeof(ar));
		strlcpy(ar.ifra_name, argv[1], sizeof(ar.ifra_name));
		if (ioctl(s, stop ? SIOCLL_STOP_X : SIOCLL_START_X, &ar) < 0 && !stop) {
			perror("SIOCLL_START");     /* may already be there; go on */
		}
	}
	if (ioctl(s, stop ? SIOCAUTOCONF_STOP_X : SIOCAUTOCONF_START_X, &ifr) < 0) {
		perror(stop ? "SIOCAUTOCONF_STOP" : "SIOCAUTOCONF_START");
		return 1;
	}
	printf("%s: IPv6 autoconf %s\n", argv[1], stop ? "stopped" : "started");
	return 0;
}
