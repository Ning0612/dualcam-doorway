#include "FaceVoter.h"
#include "config.h"

VoteResult FaceVoter::update(FaceResult raw, unsigned long rawMs, unsigned long now) {
  if (rawMs == _lastSampleMs) return VoteResult::NONE;
  _lastSampleMs = rawMs;

  const bool facePresent = (raw == FaceResult::KNOWN || raw == FaceResult::UNKNOWN);

  if (!facePresent) {
    // Face absent -- silently discard window once idle threshold passes.
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
    _knownCount++;
    _unknownStartMs = 0;  // KNOWN interrupts the sustained-UNKNOWN timer
    if (_knownCount >= FACE_VOTE_KNOWN_MIN && !_knownConfirmed) {
      _knownConfirmed = true;
      Serial.printf("[FaceVoter] KNOWN confirmed (%d hit(s))\n", _knownCount);
      return VoteResult::KNOWN_CONFIRMED;
      // No reset: window stays active, _knownConfirmed suppresses further events
      // until the face idles out and reset() is called.
    }
  } else {  // UNKNOWN
    // Only track sustained-UNKNOWN if no KNOWN has been confirmed in this window.
    if (!_knownConfirmed) {
      if (_unknownStartMs == 0) _unknownStartMs = now;
      if (now - _lastProgressMs >= 10000UL) {
        _lastProgressMs = now;
        Serial.printf("[FaceVoter] window %lus / %lus - UNKNOWN persisting\n",
                      (now - _unknownStartMs) / 1000UL,
                      (unsigned long)(FACE_VOTE_WINDOW_MS / 1000UL));
      }
      if (now - _unknownStartMs >= FACE_VOTE_WINDOW_MS) {
        Serial.println("[FaceVoter] UNKNOWN confirmed: 30s sustained");
        unsigned long saved = _lastSampleMs;
        reset();
        _lastSampleMs = saved;
        return VoteResult::UNKNOWN_CONFIRMED;
      }
    }
  }

  return VoteResult::NONE;
}

void FaceVoter::reset() {
  _windowStartMs  = 0;
  _unknownStartMs = 0;
  _lastFaceMs     = 0;
  _lastProgressMs = 0;
  _knownCount     = 0;
  _active         = false;
  _knownConfirmed = false;
  // _lastSampleMs preserved -- avoids reprocessing the frame that triggered reset
}
