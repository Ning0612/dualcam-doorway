#include "FaceVoter.h"
#include "config.h"

VoteResult FaceVoter::update(FaceResult raw, unsigned long rawMs, unsigned long now) {
  if (rawMs == _lastSampleMs) return VoteResult::NONE;
  _lastSampleMs = rawMs;

  const bool facePresent = (raw == FaceResult::KNOWN || raw == FaceResult::UNKNOWN);

  if (!facePresent) {
    if (_active && (now - _lastFaceMs >= FACE_VOTE_IDLE_MS)) {
      reset();
    }
    return VoteResult::NONE;
  }

  _lastFaceMs = now;
  if (!_active) {
    _active         = true;
    _windowStartMs  = now;
    _lastProgressMs = now;
    Serial.println("[FaceVoter] vote window started");
  }

  if (raw == FaceResult::KNOWN) {
    if (!_knownConfirmed) {
      if (_knownCount == 0) {
        _firstKnownMs = now;
        _knownCount   = 1;
      } else if (now - _firstKnownMs > FACE_VOTE_KNOWN_WINDOW_MS) {
        // Burst window expired; this hit opens a fresh burst.
        _firstKnownMs = now;
        _knownCount   = 1;
      } else {
        _knownCount++;
      }

      if (_knownCount >= FACE_VOTE_KNOWN_MIN) {
        _knownConfirmed = true;
        _unknownStartMs = 0;  // confirmed identity clears the UNKNOWN timer
        _unknownHits    = 0;
        Serial.printf("[FaceVoter] KNOWN confirmed (%d hit(s) in %lums)\n",
                      _knownCount, now - _firstKnownMs);
        return VoteResult::KNOWN_CONFIRMED;
      }
    }
    // Single KNOWN hits (below threshold) do not clear the UNKNOWN timer to
    // prevent false-known events from suppressing unknown-visitor alerts.
  } else {  // UNKNOWN
    if (!_knownConfirmed) {
      if (_unknownStartMs == 0) _unknownStartMs = now;
      _unknownHits++;

      if (now - _lastProgressMs >= 10000UL) {
        _lastProgressMs = now;
        Serial.printf("[FaceVoter] UNKNOWN %lus / %lus - %d hit(s)\n",
                      (now - _unknownStartMs) / 1000UL,
                      (unsigned long)(FACE_VOTE_WINDOW_MS / 1000UL),
                      _unknownHits);
      }

      if (now - _unknownStartMs >= FACE_VOTE_WINDOW_MS &&
          _unknownHits >= FACE_VOTE_UNKNOWN_MIN_HITS) {
        Serial.printf("[FaceVoter] UNKNOWN confirmed: %lus sustained, %d hit(s)\n",
                      (now - _unknownStartMs) / 1000UL, _unknownHits);
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
  s.active           = _active;
  s.knownConfirmed   = _knownConfirmed;
  s.knownCount       = _knownCount;
  s.knownMin         = FACE_VOTE_KNOWN_MIN;
  s.unknownHits      = _unknownHits;
  s.unknownElapsedMs = (_unknownStartMs > 0) ? (now - _unknownStartMs) : 0;
  s.unknownWindowMs  = FACE_VOTE_WINDOW_MS;
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
  _windowStartMs   = 0;
  _firstKnownMs    = 0;
  _unknownStartMs  = 0;
  _lastFaceMs      = 0;
  _lastProgressMs  = 0;
  _knownCount      = 0;
  _unknownHits     = 0;
  _active          = false;
  _knownConfirmed  = false;
  _confirmedName[0] = '\0';
  // _lastSampleMs preserved -- avoids reprocessing the frame that triggered reset
}
