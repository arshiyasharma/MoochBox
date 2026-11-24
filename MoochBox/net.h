// WiFi: connect to the owner's phone hotspot (station mode).
// The MoochBox access point for guests comes in the next milestone.
#pragma once

void netScanAndPrint();            // list nearby networks with their exact names
bool netConnectHotspot(unsigned long timeoutMs);
bool netIsConnected();
