/*
 * sweet-wifi: command-line Wi-Fi manager for sunfish under iOS
 *
 * Usage:
 *   sweet-wifi status                      Show active connection info
 *   sweet-wifi scan                        List available networks
 *   sweet-wifi saved                       List remembered networks
 *   sweet-wifi connect <SSID> [PASSWORD]   Connect to a network (uses saved password if omitted)
 *   sweet-wifi forget <SSID>               Remove a remembered network
 *   sweet-wifi disconnect                  Disconnect from current network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <CommonCrypto/CommonKeyDerivation.h>

#define CONF_PATH       "/fw/wlan/net.conf"
#define CONF_BAK        "/fw/wlan/net.conf.bak"
#define SAVED_PATH      "/fw/wlan/saved.conf"

static void
seed_saved_from_net_conf(void)
{
	FILE *fp = fopen(CONF_PATH, "r");
	if (fp == NULL) return;

	char cur_ssid[64] = {0}, cur_pmk[128] = {0};
	if (fscanf(fp, "%63s %127s", cur_ssid, cur_pmk) == 2 && strlen(cur_ssid) > 0 && strlen(cur_pmk) == 64) {
		FILE *sfp = fopen(SAVED_PATH, "r");
		bool found = false;
		if (sfp != NULL) {
			char s_ssid[64], s_pmk[128];
			while (fscanf(sfp, "%63s %127s", s_ssid, s_pmk) == 2) {
				if (strcmp(s_ssid, cur_ssid) == 0) {
					found = true;
					break;
				}
			}
			fclose(sfp);
		}
		if (!found) {
			sfp = fopen(SAVED_PATH, "a");
			if (sfp != NULL) {
				fprintf(sfp, "%s %s\n", cur_ssid, cur_pmk);
				fclose(sfp);
			}
		}
	}
	fclose(fp);
}

static int
cmd_status(void)
{
	char buf[256] = {0};
	size_t len = sizeof(buf) - 1;

	if (sysctlbyname("kern.sweet_wlan_info", buf, &len, NULL, 0) < 0 || len == 0 || buf[0] == 0) {
		printf("Wi-Fi status: Disconnected / Inactive\n");
		return 0;
	}

	char ip[64] = {0}, mask[64] = {0}, router[64] = {0}, dns[64] = {0}, ssid[64] = {0};
	if (sscanf(buf, "%63s %63s %63s %63s %63s", ip, mask, router, dns, ssid) < 4) {
		printf("Wi-Fi status: Connecting (%s)...\n", buf);
		return 0;
	}

	printf("Interface:   ew0\n");
	printf("Status:      Connected\n");
	printf("SSID:        %s\n", ssid[0] ? ssid : "(unknown)");
	printf("IP Address:  %s\n", ip);
	printf("Subnet Mask: %s\n", mask);
	printf("Router:      %s\n", router);
	printf("DNS Server:  %s\n", dns);
	return 0;
}

static int
cmd_scan(void)
{
	char buf[4096] = {0};
	size_t len = sizeof(buf) - 1;

	if (sysctlbyname("kern.sweet_wlan_scan", buf, &len, NULL, 0) == 0 && len > 0 && buf[0] != 0) {
		printf("%-18s  %-5s  %-6s  %-8s  %s\n", "BSSID", "CHAN", "FREQ", "RSSI", "SSID");
		printf("------------------  -----  ------  --------  --------------------\n");
		char *line = strtok(buf, "\n");
		while (line != NULL) {
			char bssid[32] = {0}, ssid[64] = {0};
			unsigned mhz = 0;
			int rssi = 0;
			if (sscanf(line, "%31s %u %d %63s", bssid, &mhz, &rssi, ssid) >= 4) {
				unsigned chan = (mhz >= 2412 && mhz <= 2484) ? (mhz - 2407) / 5 : (mhz - 5000) / 5;
				printf("%-18s  %-5u  %-4uMHz  %-5d dBm  %s\n", bssid, chan, mhz, rssi, ssid);
			}
			line = strtok(NULL, "\n");
		}
		return 0;
	}

	/* Fallback: parse last scan from kernel events */
	len = 16384;
	char *events = malloc(len + 1);
	if (events != NULL && sysctlbyname("kern.sweet_mpss_events", events, &len, NULL, 0) == 0) {
		events[len] = 0;
		printf("%-18s  %-5s  %-6s  %-8s  %s\n", "BSSID", "CHAN", "FREQ", "RSSI", "SSID");
		printf("------------------  -----  ------  --------  --------------------\n");
		char *p = events;
		while ((p = strstr(p, "BSS ")) != NULL) {
			p += 4;
			char bssid_raw[16] = {0}, ssid[64] = {0};
			unsigned mhz = 0;
			int rssi = 0;
			if (sscanf(p, "%12s %u %d \"%63[^\"]\"", bssid_raw, &mhz, &rssi, ssid) >= 4) {
				char bssid[20];
				snprintf(bssid, sizeof(bssid), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
				    bssid_raw[0], bssid_raw[1], bssid_raw[2], bssid_raw[3],
				    bssid_raw[4], bssid_raw[5], bssid_raw[6], bssid_raw[7],
				    bssid_raw[8], bssid_raw[9], bssid_raw[10], bssid_raw[11]);
				unsigned chan = (mhz >= 2412 && mhz <= 2484) ? (mhz - 2407) / 5 : (mhz - 5000) / 5;
				printf("%-18s  %-5u  %-4uMHz  %-5d dBm  %s\n", bssid, chan, mhz, rssi, ssid);
			}
		}
		free(events);
		return 0;
	}
	if (events) free(events);

	printf("No scan results available in kernel.\n");
	return 1;
}

static int
cmd_saved(void)
{
	seed_saved_from_net_conf();
	FILE *fp = fopen(SAVED_PATH, "r");
	if (fp == NULL) {
		printf("No saved networks found.\n");
		return 0;
	}

	printf("Remembered Wi-Fi networks:\n");
	printf("----------------------------------------\n");
	char ssid[64], pmk[128];
	int count = 0;
	while (fscanf(fp, "%63s %127s", ssid, pmk) == 2) {
		printf("  * %s\n", ssid);
		count++;
	}
	fclose(fp);
	if (count == 0) {
		printf("  (none)\n");
	}
	return 0;
}

static int
save_network(const char *ssid, const char *pmk_hex)
{
	seed_saved_from_net_conf();
	FILE *fp = fopen(SAVED_PATH, "r");
	char lines[64][192];
	int nlines = 0;
	bool updated = false;

	if (fp != NULL) {
		char s_ssid[64], s_pmk[128];
		while (nlines < 64 && fscanf(fp, "%63s %127s", s_ssid, s_pmk) == 2) {
			if (strcmp(s_ssid, ssid) == 0) {
				snprintf(lines[nlines++], sizeof(lines[0]), "%s %s\n", ssid, pmk_hex);
				updated = true;
			} else {
				snprintf(lines[nlines++], sizeof(lines[0]), "%s %s\n", s_ssid, s_pmk);
			}
		}
		fclose(fp);
	}

	if (!updated && nlines < 64) {
		snprintf(lines[nlines++], sizeof(lines[0]), "%s %s\n", ssid, pmk_hex);
	}

	fp = fopen(SAVED_PATH, "w");
	if (fp == NULL) return -1;
	for (int i = 0; i < nlines; i++) {
		fputs(lines[i], fp);
	}
	fclose(fp);
	return 0;
}

static int
cmd_forget(const char *ssid)
{
	seed_saved_from_net_conf();
	FILE *fp = fopen(SAVED_PATH, "r");
	if (fp == NULL) {
		printf("No saved networks.\n");
		return 0;
	}

	char lines[64][192];
	int nlines = 0;
	bool found = false;
	char s_ssid[64], s_pmk[128];
	while (nlines < 64 && fscanf(fp, "%63s %127s", s_ssid, s_pmk) == 2) {
		if (strcmp(s_ssid, ssid) == 0) {
			found = true;
		} else {
			snprintf(lines[nlines++], sizeof(lines[0]), "%s %s\n", s_ssid, s_pmk);
		}
	}
	fclose(fp);

	if (!found) {
		printf("Network \"%s\" not found in remembered networks.\n", ssid);
		return 1;
	}

	fp = fopen(SAVED_PATH, "w");
	if (fp != NULL) {
		for (int i = 0; i < nlines; i++) {
			fputs(lines[i], fp);
		}
		fclose(fp);
	}
	printf("Removed \"%s\" from remembered networks.\n", ssid);
	return 0;
}

static int
find_saved_pmk(const char *ssid, char *out_pmk, size_t max_len)
{
	seed_saved_from_net_conf();
	FILE *fp = fopen(SAVED_PATH, "r");
	if (fp == NULL) return -1;

	char s_ssid[64], s_pmk[128];
	while (fscanf(fp, "%63s %127s", s_ssid, s_pmk) == 2) {
		if (strcmp(s_ssid, ssid) == 0) {
			strncpy(out_pmk, s_pmk, max_len - 1);
			out_pmk[max_len - 1] = 0;
			fclose(fp);
			return 0;
		}
	}
	fclose(fp);
	return -1;
}

static int
cmd_connect(const char *ssid, const char *pass)
{
	if (strlen(ssid) == 0 || strlen(ssid) > 32) {
		fprintf(stderr, "Error: SSID must be between 1 and 32 characters\n");
		return 1;
	}

	char pmk_hex[65] = {0};

	if (pass == NULL || strlen(pass) == 0) {
		if (find_saved_pmk(ssid, pmk_hex, sizeof(pmk_hex)) != 0) {
			fprintf(stderr, "Network \"%s\" not found in remembered networks.\n", ssid);
			fprintf(stderr, "Usage: sweet-wifi connect <SSID> <PASSWORD>\n");
			return 1;
		}
		printf("Using saved credentials for \"%s\"...\n", ssid);
	} else {
		if (strlen(pass) < 8 || strlen(pass) > 63) {
			fprintf(stderr, "Error: WPA2-PSK password must be between 8 and 63 characters\n");
			return 1;
		}

		printf("Computing WPA2 PMK for \"%s\" (PBKDF2 4096 rounds)...\n", ssid);
		uint8_t pmk[32];
		int ret = CCKeyDerivationPBKDF(kCCPBKDF2, pass, strlen(pass),
		    (const uint8_t *)ssid, strlen(ssid),
		    kCCPRFHmacAlgSHA1, 4096, pmk, sizeof(pmk));
		if (ret != 0) {
			fprintf(stderr, "Error: CCKeyDerivationPBKDF failed (%d)\n", ret);
			return 1;
		}

		for (int i = 0; i < 32; i++) {
			snprintf(pmk_hex + i * 2, 3, "%02x", pmk[i]);
		}
		save_network(ssid, pmk_hex);
	}

	/* Backup existing net.conf */
	FILE *bfp = fopen(CONF_PATH, "r");
	char bak_data[256] = {0};
	if (bfp != NULL) {
		size_t nr = fread(bak_data, 1, sizeof(bak_data) - 1, bfp);
		bak_data[nr] = 0;
		fclose(bfp);
		FILE *obak = fopen(CONF_BAK, "w");
		if (obak != NULL) {
			fwrite(bak_data, 1, nr, obak);
			fclose(obak);
		}
	}

	FILE *fp = fopen(CONF_PATH, "w");
	if (fp == NULL) {
		perror("Error opening " CONF_PATH);
		return 1;
	}
	fprintf(fp, "%s\n%s\n", ssid, pmk_hex);
	fclose(fp);

	printf("Wrote " CONF_PATH " successfully.\n");
	printf("Triggering Wi-Fi rejoin (kern.sweet_wlan_kick=1)...\n");

	int kick = 1;
	if (sysctlbyname("kern.sweet_wlan_kick", NULL, NULL, &kick, sizeof(kick)) < 0) {
		perror("sysctl kern.sweet_wlan_kick failed");
		return 1;
	}

	printf("Associating with \"%s\"...", ssid);
	fflush(stdout);

	for (int i = 0; i < 20; i++) {
		sleep(1);
		char buf[256] = {0};
		size_t len = sizeof(buf) - 1;
		if (sysctlbyname("kern.sweet_wlan_info", buf, &len, NULL, 0) == 0 && len > 0) {
			char cur_ip[64] = {0}, cur_mask[64] = {0}, cur_router[64] = {0}, cur_dns[64] = {0}, cur_ssid[64] = {0};
			if (sscanf(buf, "%63s %63s %63s %63s %63s", cur_ip, cur_mask, cur_router, cur_dns, cur_ssid) >= 4) {
				if (strcmp(cur_ssid, ssid) == 0) {
					printf("\nSuccessfully connected to \"%s\"!\n", ssid);
					printf("IP: %s, Gateway: %s, DNS: %s\n", cur_ip, cur_router, cur_dns);
					return 0;
				}
			}
		}
		printf(".");
		fflush(stdout);
	}

	printf("\nTimed out waiting for connection.\n");
	if (bak_data[0] != 0) {
		printf("Restoring previous network configuration...\n");
		FILE *rfp = fopen(CONF_PATH, "w");
		if (rfp != NULL) {
			fputs(bak_data, rfp);
			fclose(rfp);
			kick = 1;
			sysctlbyname("kern.sweet_wlan_kick", NULL, NULL, &kick, sizeof(kick));
		}
	}
	printf("Check `sysctl kern.sweet_mpss_events` for details.\n");
	return 1;
}

static int
cmd_disconnect(void)
{
	printf("Disconnecting from Wi-Fi...\n");
	/* Wipe credentials from net.conf so station loop remains idle */
	FILE *fp = fopen(CONF_PATH, "w");
	if (fp != NULL) {
		fprintf(fp, "\n\n");
		fclose(fp);
	}
	int kick = 1;
	sysctlbyname("kern.sweet_wlan_kick", NULL, NULL, &kick, sizeof(kick));
	printf("Disconnected.\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Usage: sweet-wifi <status|scan|saved|connect <SSID> [PASS]|forget <SSID>|disconnect>\n");
		return 1;
	}

	if (strcmp(argv[1], "status") == 0) {
		return cmd_status();
	} else if (strcmp(argv[1], "scan") == 0) {
		return cmd_scan();
	} else if (strcmp(argv[1], "saved") == 0) {
		return cmd_saved();
	} else if (strcmp(argv[1], "forget") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: sweet-wifi forget <SSID>\n");
			return 1;
		}
		return cmd_forget(argv[2]);
	} else if (strcmp(argv[1], "connect") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: sweet-wifi connect <SSID> [PASSWORD]\n");
			return 1;
		}
		return cmd_connect(argv[2], argc >= 4 ? argv[3] : NULL);
	} else if (strcmp(argv[1], "disconnect") == 0) {
		return cmd_disconnect();
	} else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		fprintf(stderr, "Usage: sweet-wifi <status|scan|saved|connect <SSID> [PASS]|forget <SSID>|disconnect>\n");
		return 1;
	}
}
