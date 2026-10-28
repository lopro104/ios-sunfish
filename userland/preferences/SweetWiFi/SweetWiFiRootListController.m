#import "SweetWiFiRootListController.h"
#import <sys/sysctl.h>
#import <spawn.h>

extern char **environ;

@interface SweetWiFiRootListController ()
@property (nonatomic, strong) NSMutableArray *scannedNetworks;
@property (nonatomic, strong) NSString *currentSSID;
@property (nonatomic, strong) NSString *currentIP;
@property (nonatomic, strong) NSString *currentRouter;
@property (nonatomic, assign) BOOL isConnected;
@end

@implementation SweetWiFiRootListController

- (void)readStatus {
    self.isConnected = NO;
    self.currentSSID = @"None";
    self.currentIP = @"0.0.0.0";
    self.currentRouter = @"0.0.0.0";

    char buf[256];
    size_t len = sizeof(buf) - 1;
    if (sysctlbyname("kern.sweet_wlan_info", buf, &len, NULL, 0) == 0 && len > 0) {
        buf[len] = '\0';
        NSString *info = [NSString stringWithUTF8String:buf];
        NSArray *parts = [info componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (parts.count >= 5) {
            self.currentIP = parts[0];
            self.currentRouter = parts[2];
            self.currentSSID = parts[4];
            self.isConnected = (![self.currentIP isEqualToString:@"0.0.0.0"] && ![self.currentIP isEqualToString:@"127.0.0.1"]);
        }
    }
}

- (NSArray *)specifiers {
    if (!_specifiers) {
        [self readStatus];
        NSMutableArray *specs = [NSMutableArray array];

        // Group: Status
        PSSpecifier *statusGroup = [PSSpecifier preferenceSpecifierNamed:@"CURRENT CONNECTION"
                                                                  target:self
                                                                     set:nil
                                                                     get:nil
                                                                  detail:nil
                                                                    cell:PSGroupCell
                                                                    edit:nil];
        [statusGroup setProperty:@"Wi-Fi link managed by WCN3990 Hexagon DSP driver." forKey:@"footerText"];
        [specs addObject:statusGroup];

        // Status Row
        PSSpecifier *statusSpec = [PSSpecifier preferenceSpecifierNamed:@"Status"
                                                                 target:self
                                                                    set:nil
                                                                    get:@selector(getStatusString:)
                                                                 detail:nil
                                                                   cell:PSTitleValueCell
                                                                   edit:nil];
        [specs addObject:statusSpec];

        // SSID Row
        PSSpecifier *ssidSpec = [PSSpecifier preferenceSpecifierNamed:@"Network (SSID)"
                                                               target:self
                                                                  set:nil
                                                                  get:@selector(getSSIDString:)
                                                               detail:nil
                                                                 cell:PSTitleValueCell
                                                                 edit:nil];
        [specs addObject:ssidSpec];

        // IP Address Row
        PSSpecifier *ipSpec = [PSSpecifier preferenceSpecifierNamed:@"IP Address"
                                                             target:self
                                                                set:nil
                                                                get:@selector(getIPString:)
                                                             detail:nil
                                                               cell:PSTitleValueCell
                                                               edit:nil];
        [specs addObject:ipSpec];

        // Router Gateway Row
        PSSpecifier *routerSpec = [PSSpecifier preferenceSpecifierNamed:@"Router Gateway"
                                                                 target:self
                                                                    set:nil
                                                                    get:@selector(getRouterString:)
                                                                 detail:nil
                                                                   cell:PSTitleValueCell
                                                                   edit:nil];
        [specs addObject:routerSpec];

        // Group: Actions
        PSSpecifier *actionGroup = [PSSpecifier preferenceSpecifierNamed:@"ACTIONS"
                                                                  target:self
                                                                     set:nil
                                                                     get:nil
                                                                  detail:nil
                                                                    cell:PSGroupCell
                                                                    edit:nil];
        [specs addObject:actionGroup];

        // Button: Refresh Status
        PSSpecifier *refreshBtn = [PSSpecifier preferenceSpecifierNamed:@"Refresh Status"
                                                                 target:self
                                                                    set:nil
                                                                    get:nil
                                                                 detail:nil
                                                                   cell:PSButtonCell
                                                                   edit:nil];
        [refreshBtn setButtonAction:@selector(refreshAction)];
        [specs addObject:refreshBtn];

        // Button: Scan Networks
        PSSpecifier *scanBtn = [PSSpecifier preferenceSpecifierNamed:@"Scan Nearby Networks"
                                                              target:self
                                                                 set:nil
                                                                 get:nil
                                                              detail:nil
                                                                cell:PSButtonCell
                                                                edit:nil];
        [scanBtn setButtonAction:@selector(scanAction)];
        [specs addObject:scanBtn];

        // Button: Reconnect (Kick)
        PSSpecifier *kickBtn = [PSSpecifier preferenceSpecifierNamed:@"Reconnect / Kick Link"
                                                              target:self
                                                                 set:nil
                                                                 get:nil
                                                              detail:nil
                                                                cell:PSButtonCell
                                                                edit:nil];
        [kickBtn setButtonAction:@selector(reconnectAction)];
        [specs addObject:kickBtn];

        // Button: Disconnect
        PSSpecifier *disconBtn = [PSSpecifier preferenceSpecifierNamed:@"Disconnect"
                                                                target:self
                                                                   set:nil
                                                                   get:nil
                                                                detail:nil
                                                                  cell:PSButtonCell
                                                                  edit:nil];
        [disconBtn setButtonAction:@selector(disconnectAction)];
        [specs addObject:disconBtn];

        // Group: Scanned Networks (if any)
        if (self.scannedNetworks && self.scannedNetworks.count > 0) {
            PSSpecifier *scanGroup = [PSSpecifier preferenceSpecifierNamed:@"AVAILABLE NETWORKS"
                                                                    target:self
                                                                       set:nil
                                                                       get:nil
                                                                    detail:nil
                                                                      cell:PSGroupCell
                                                                      edit:nil];
            [scanGroup setProperty:@"Tap a network to connect." forKey:@"footerText"];
            [specs addObject:scanGroup];

            for (NSDictionary *net in self.scannedNetworks) {
                NSString *ssid = net[@"ssid"];
                NSString *rssi = net[@"rssi"];
                NSString *title = [NSString stringWithFormat:@"%@ (%@ dBm)", ssid, rssi];
                PSSpecifier *netBtn = [PSSpecifier preferenceSpecifierNamed:title
                                                                     target:self
                                                                        set:nil
                                                                        get:nil
                                                                     detail:nil
                                                                       cell:PSButtonCell
                                                                       edit:nil];
                [netBtn setProperty:ssid forKey:@"sweetSSID"];
                [netBtn setButtonAction:@selector(connectToNetworkFromSpecifier:)];
                [specs addObject:netBtn];
            }
        }

        // Group: About
        PSSpecifier *aboutGroup = [PSSpecifier preferenceSpecifierNamed:@"SYSTEM & HARDWARE"
                                                                 target:self
                                                                    set:nil
                                                                    get:nil
                                                                 detail:nil
                                                                   cell:PSGroupCell
                                                                   edit:nil];
        [specs addObject:aboutGroup];

        PSSpecifier *hwSpec = [PSSpecifier preferenceSpecifierNamed:@"Chipset"
                                                             target:self
                                                                set:nil
                                                                get:@selector(getChipsetString:)
                                                             detail:nil
                                                               cell:PSTitleValueCell
                                                               edit:nil];
        [specs addObject:hwSpec];

        PSSpecifier *protoSpec = [PSSpecifier preferenceSpecifierNamed:@"Protocol"
                                                                target:self
                                                                   set:nil
                                                                   get:@selector(getProtocolString:)
                                                                detail:nil
                                                                  cell:PSTitleValueCell
                                                                  edit:nil];
        [specs addObject:protoSpec];

        _specifiers = specs;
    }
    return _specifiers;
}

- (id)getStatusString:(PSSpecifier *)spec {
    return self.isConnected ? @"Connected" : @"Disconnected";
}

- (id)getSSIDString:(PSSpecifier *)spec {
    return self.currentSSID;
}

- (id)getIPString:(PSSpecifier *)spec {
    return self.currentIP;
}

- (id)getRouterString:(PSSpecifier *)spec {
    return self.currentRouter;
}

- (id)getChipsetString:(PSSpecifier *)spec {
    return @"Qualcomm WCN3990 (SNOC)";
}

- (id)getProtocolString:(PSSpecifier *)spec {
    return @"802.11n / HT (WPA2-PSK)";
}

- (void)refreshAction {
    self.specifiers = nil;
    [self reloadSpecifiers];
}

- (void)reconnectAction {
    int val = 1;
    sysctlbyname("kern.sweet_wlan_kick", NULL, NULL, &val, sizeof(val));
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self refreshAction];
    });
}

- (void)disconnectAction {
    pid_t pid;
    char *argv[] = { "/var/jb/usr/bin/sweet-wifi", "disconnect", NULL };
    posix_spawn(&pid, "/var/jb/usr/bin/sweet-wifi", NULL, NULL, argv, environ);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self refreshAction];
    });
}

- (void)scanAction {
    FILE *fp = popen("/var/jb/usr/bin/sweet-wifi scan", "r");
    if (fp) {
        NSMutableArray *nets = [NSMutableArray array];
        char line[256];
        // Skip header
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                NSString *str = [[NSString stringWithUTF8String:line] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                NSArray *tokens = [str componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
                NSMutableArray *nonEmpty = [NSMutableArray array];
                for (NSString *t in tokens) {
                    if (t.length > 0) [nonEmpty addObject:t];
                }
                if (nonEmpty.count >= 5) {
                    NSString *rssi = nonEmpty[3];
                    // Rest is SSID
                    NSArray *ssidParts = [nonEmpty subarrayWithRange:NSMakeRange(4, nonEmpty.count - 4)];
                    NSString *ssid = [ssidParts componentsJoinedByString:@" "];
                    if (ssid.length > 0) {
                        [nets addObject:@{ @"ssid": ssid, @"rssi": rssi }];
                    }
                }
            }
        }
        pclose(fp);
        self.scannedNetworks = nets;
    }
    self.specifiers = nil;
    [self reloadSpecifiers];
}

- (void)connectToNetworkFromSpecifier:(PSSpecifier *)spec {
    NSString *ssid = [spec propertyForKey:@"sweetSSID"];
    if (!ssid) return;

    UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Join \"%@\"", ssid]
                                                                   message:@"Enter the Wi-Fi password for this network:"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
        textField.placeholder = @"Password";
        textField.secureTextEntry = YES;
    }];

    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil];
    UIAlertAction *joinAction = [UIAlertAction actionWithTitle:@"Join" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        NSString *password = alert.textFields.firstObject.text ?: @"";
        pid_t pid;
        char *argv[] = {
            "/var/jb/usr/bin/sweet-wifi",
            "connect",
            (char *)[ssid UTF8String],
            (char *)[password UTF8String],
            NULL
        };
        posix_spawn(&pid, "/var/jb/usr/bin/sweet-wifi", NULL, NULL, argv, environ);

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self refreshAction];
        });
    }];

    [alert addAction:cancelAction];
    [alert addAction:joinAction];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
