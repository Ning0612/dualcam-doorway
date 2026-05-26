#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "SecurityStateMachine.h"
#include "FaceVoter.h"
#include "LogManager.h"

class DashboardServer {
public:
  // Register all dashboard routes on server.
  // faceVoter:  pointer to the FaceVoter instance; may not be nullptr.
  // logManager: pointer to the LogManager instance; may not be nullptr.
  static void begin(WebServer& server,
                    SecurityStateMachine& sm,
                    const char* agentLabel,
                    FaceVoter* faceVoter,
                    LogManager* logManager);
};
