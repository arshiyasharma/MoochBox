// WiFi (plan §3.2): station (owner's hotspot, for Spotify) + access point
// (the MoochBox network guests join) at the same time, plus captive-portal DNS.
#pragma once
#include <IPAddress.h>

extern const IPAddress MOOCHBOX_IP;  // 192.168.4.1

void netScanAndPrint();            // list nearby networks with their exact names
bool netConnectHotspot(unsigned long timeoutMs);
bool netIsConnected();
void netStartAccessPoint();        // MoochBox network + DNS that points every name at us
void netLoop();                    // call every loop (answers DNS)
