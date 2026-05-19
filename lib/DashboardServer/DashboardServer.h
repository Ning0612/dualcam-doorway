#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "AgentProtocol.h"  // for PeerStatus

class DoorStateMachine;

class DashboardServer {
public:
  // Register all dashboard routes on server.
  // doorOpen: pointer to the door state bool (indoor only); pass nullptr for outdoor.
  // hallRaw:  pointer to the latest raw Hall ADC reading (indoor only); nullptr for outdoor.
  // hallRaw:       pointer to latest ADC reading (read-only display).
  // hallThreshold: pointer to the runtime threshold variable in indoor_main;
  //                updated in-place when the user saves a new value via /settings.
  static void begin(WebServer& server,
                    DoorStateMachine& sm,
                    const char* agentLabel,
                    PeerStatus* cachedPeer,
                    bool* doorOpen = nullptr,
                    uint16_t* hallRaw = nullptr,
                    uint16_t* hallThreshold = nullptr);
};
