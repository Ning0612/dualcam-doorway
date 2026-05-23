#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "AgentProtocol.h"  // for PeerStatus
#include "FaceVoter.h"      // for FaceVoter* (outdoor only; nullptr for indoor)

class DoorStateMachine;

class DashboardServer {
public:
  // Register all dashboard routes on server.
  // doorOpen:  pointer to door state bool (indoor only); nullptr for outdoor.
  // hallRaw:   pointer to latest raw Hall ADC reading (indoor only); nullptr for outdoor.
  // hallThreshold: pointer to runtime threshold in indoor_main; updated in-place on save.
  // faceVoter: pointer to the FaceVoter instance (outdoor only); nullptr for indoor.
  static void begin(WebServer& server,
                    DoorStateMachine& sm,
                    const char* agentLabel,
                    PeerStatus* cachedPeer,
                    bool* doorOpen       = nullptr,
                    uint16_t* hallRaw    = nullptr,
                    uint16_t* hallThreshold = nullptr,
                    FaceVoter* faceVoter = nullptr);
};
