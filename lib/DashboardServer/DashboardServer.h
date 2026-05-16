#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "AgentProtocol.h"  // for PeerStatus

class DoorStateMachine;

class DashboardServer {
public:
  // Register all dashboard routes on server.
  // doorOpen: pointer to the door state bool (indoor only); pass nullptr for outdoor.
  static void begin(WebServer& server,
                    DoorStateMachine& sm,
                    const char* agentLabel,
                    PeerStatus* cachedPeer,
                    bool* doorOpen = nullptr);
};
