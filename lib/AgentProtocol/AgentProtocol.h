#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "states.h"

struct PeerStatus {
  bool          online;
  SystemState   state;
  unsigned long updatedAt;
};

class DoorStateMachine;

class AgentProtocol {
public:
  // Register this agent's REST endpoint on the shared WebServer.
  // agentLabel "Indoor"  → GET /home_state
  // agentLabel "Outdoor" → GET /outside_status
  static void registerRoutes(WebServer& server, DoorStateMachine& sm,
                              const char* agentLabel);

  // Query the peer agent. path is "/home_state" or "/outside_status".
  // Returns true and fills out on success; sets out.online=false on failure.
  static bool queryPeer(const char* peerIp, const char* path, PeerStatus& out);
};
