#include "AgentProtocol.h"
#include "DoorStateMachine.h"
#include "messages.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static DoorStateMachine* _sm          = nullptr;
static const char*       _agentLabel  = nullptr;

static void sendStatusResponse(WebServer& server) {
  const char* type = (strcmp(_agentLabel, "Indoor") == 0)
                     ? TYPE_HOME_STATE : TYPE_OUTSIDE_STATUS;

  JsonDocument doc;
  doc[MSG_TYPE]      = type;
  doc[MSG_STATE]     = stateToString(_sm->getState());
  doc[MSG_TIMESTAMP] = millis();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void AgentProtocol::registerRoutes(WebServer& server, DoorStateMachine& sm,
                                    const char* agentLabel) {
  _sm         = &sm;
  _agentLabel = agentLabel;

  if (strcmp(agentLabel, "Indoor") == 0) {
    server.on("/home_state", HTTP_GET, [&server]() {
      sendStatusResponse(server);
    });
    Serial.println("[AgentProtocol] Registered GET /home_state");
  } else {
    server.on("/outside_status", HTTP_GET, [&server]() {
      sendStatusResponse(server);
    });
    Serial.println("[AgentProtocol] Registered GET /outside_status");
  }
}

bool AgentProtocol::queryPeer(const char* peerIp, const char* path, PeerStatus& out) {
  if (WiFi.status() != WL_CONNECTED) {
    out.online = false;
    return false;
  }

  WiFiClient  wifiClient;
  HTTPClient  http;
  String      url = String("http://") + peerIp + path;

  http.begin(wifiClient, url);
  http.setTimeout(3000);
  int code = http.GET();

  if (code != 200) {
    http.end();
    out.online    = false;
    out.updatedAt = millis();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    out.online = false;
    return false;
  }

  out.online    = true;
  out.updatedAt = millis();

  const char* s = doc[MSG_STATE] | "IDLE";
  if      (strcmp(s, "IDLE")             == 0) out.state = SystemState::IDLE;
  else if (strcmp(s, "PREPARE_TO_LEAVE") == 0) out.state = SystemState::PREPARE_TO_LEAVE;
  else if (strcmp(s, "PREPARE_TO_ENTER") == 0) out.state = SystemState::PREPARE_TO_ENTER;
  else if (strcmp(s, "LEAVING_HOME")     == 0) out.state = SystemState::LEAVING_HOME;
  else if (strcmp(s, "ENTERING_HOME")    == 0) out.state = SystemState::ENTERING_HOME;
  else if (strcmp(s, "HOME_OCCUPIED")    == 0) out.state = SystemState::HOME_OCCUPIED;
  else if (strcmp(s, "HOME_EMPTY")       == 0) out.state = SystemState::HOME_EMPTY;
  else if (strcmp(s, "UNKNOWN_VISITOR")  == 0) out.state = SystemState::UNKNOWN_VISITOR;
  else if (strcmp(s, "ALERT_MODE")       == 0) out.state = SystemState::ALERT_MODE;
  else                                          out.state = SystemState::IDLE;

  return true;
}
