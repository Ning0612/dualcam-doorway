#include "FaceVoter.h"
#include "config.h"
#include <string.h>

VoteResult FaceVoter::update(FaceResult raw, unsigned long rawMs, unsigned long now) {
  if (rawMs == _lastSampleMs) return VoteResult::NONE;
  _lastSampleMs = rawMs;

  // DETECTED = face found but no enrolled faces to match — treat as UNKNOWN for voting.
  const bool facePresent = (raw == FaceResult::KNOWN ||
                            raw == FaceResult::UNKNOWN ||
                            raw == FaceResult::DETECTED);

  if (!facePresent) {
    if (_active && (now - _lastFaceMs >= FACE_VOTE_IDLE_MS)) {
      reset();
    }
    return VoteResult::NONE;
  }

  _lastFaceMs = now;
  if (!_active) {
    _active         = true;
    _lastProgressMs = now;
    Serial.println("[FaceVoter] vote window started");
  }

  if (raw == FaceResult::KNOWN) {
    if (!_knownConfirmed) {
      // Push to KNOWN ring buffer
      _knownTs[_knownHead] = now;
      _knownHead = (_knownHead + 1) % KNOWN_BUF;
      if (_knownFill < KNOWN_BUF) _knownFill++;

      // Sliding window: are the last KNOWN_BUF hits all within FACE_VOTE_KNOWN_WINDOW_MS?
      if (_knownFill >= KNOWN_BUF) {
        unsigned long oldest = _knownTs[_oldestIdx(_knownHead, _knownFill, KNOWN_BUF)];
        if (now - oldest <= FACE_VOTE_KNOWN_WINDOW_MS) {
          _knownConfirmed = true;
          _unknownFill    = 0;  // confirmed identity clears UNKNOWN ring buffer
          _unknownHead    = 0;
          Serial.printf("[FaceVoter] KNOWN confirmed (%d hit(s) in %lums)\n",
                        _knownFill, now - oldest);
          return VoteResult::KNOWN_CONFIRMED;
        }
      }
      // Pre-confirmation: single KNOWN hits do not clear the UNKNOWN ring buffer to
      // prevent false-known frames from suppressing unknown-visitor alerts.
    } else {
      // Post-confirmation: known user is still present — clear any accumulated UNKNOWN
      // hits so recognition failures don't trigger a false UNKNOWN_CONFIRMED alarm.
      _unknownFill = 0;
      _unknownHead = 0;
    }

  } else {  // UNKNOWN or DETECTED (unmatched face, including no-enrollment case)
    // Always track UNKNOWN hits regardless of _knownConfirmed so a stranger appearing
    // immediately after a confirmed known user still accumulates toward UNKNOWN_CONFIRMED.
    _unknownTs[_unknownHead] = now;
    _unknownHead = (_unknownHead + 1) % UNKNOWN_BUF;
    if (_unknownFill < UNKNOWN_BUF) _unknownFill++;

    if (now - _lastProgressMs >= 10000UL) {
      _lastProgressMs = now;
      int           oi        = _oldestIdx(_unknownHead, _unknownFill, UNKNOWN_BUF);
      unsigned long elapsedMs = (_unknownFill > 0) ? (now - _unknownTs[oi]) : 0;
      Serial.printf("[FaceVoter] UNKNOWN %lus / %lus - %d/%d hit(s)%s\n",
                    elapsedMs / 1000UL,
                    (unsigned long)(FACE_VOTE_WINDOW_MS / 1000UL),
                    _unknownFill, UNKNOWN_BUF,
                    _knownConfirmed ? " (post-known)" : "");
    }

    // Sliding window: are the last UNKNOWN_BUF hits all within FACE_VOTE_WINDOW_MS?
    if (_unknownFill >= UNKNOWN_BUF) {
      unsigned long oldest = _unknownTs[_oldestIdx(_unknownHead, _unknownFill, UNKNOWN_BUF)];
      if (now - oldest <= FACE_VOTE_WINDOW_MS) {
        Serial.printf("[FaceVoter] UNKNOWN confirmed: oldest %lus ago, %d hit(s)\n",
                      (now - oldest) / 1000UL, _unknownFill);
        unsigned long saved = _lastSampleMs;
        reset();
        _lastSampleMs = saved;
        return VoteResult::UNKNOWN_CONFIRMED;
      }
    }
  }

  return VoteResult::NONE;
}

FaceVoterStatus FaceVoter::getStatus(unsigned long now) const {
  FaceVoterStatus s;
  s.active          = _active;
  s.knownConfirmed  = _knownConfirmed;
  s.knownCount      = _knownFill;
  s.knownMin        = FACE_VOTE_KNOWN_MIN;
  s.unknownHits     = _unknownFill;
  s.unknownWindowMs = FACE_VOTE_WINDOW_MS;
  if (_unknownFill > 0) {
    int oi = _oldestIdx(_unknownHead, _unknownFill, UNKNOWN_BUF);
    s.unknownElapsedMs = now - _unknownTs[oi];
  } else {
    s.unknownElapsedMs = 0;
  }
  strncpy(s.confirmedName, _confirmedName, 16);
  s.confirmedName[16] = '\0';
  return s;
}

void FaceVoter::setConfirmedName(const char* name) {
  if (name && name[0]) {
    strncpy(_confirmedName, name, 16);
    _confirmedName[16] = '\0';
  } else {
    _confirmedName[0] = '\0';
  }
}

void FaceVoter::reset() {
  memset(_knownTs,   0, sizeof(_knownTs));
  memset(_unknownTs, 0, sizeof(_unknownTs));
  _knownHead       = 0;
  _unknownHead     = 0;
  _knownFill       = 0;
  _unknownFill     = 0;
  _lastFaceMs      = 0;
  _lastProgressMs  = 0;
  _active          = false;
  _knownConfirmed  = false;
  _confirmedName[0] = '\0';
  // _lastSampleMs preserved — avoids reprocessing the frame that triggered reset
}
