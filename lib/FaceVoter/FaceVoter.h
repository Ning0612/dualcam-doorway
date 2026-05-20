#pragma once
#include <Arduino.h>
#include "CameraAgent.h"

enum class VoteResult { NONE, KNOWN_CONFIRMED, UNKNOWN_CONFIRMED };

// Temporal voting window to reduce single-frame UNKNOWN false alarms.
//
// KNOWN_CONFIRMED fires once when FACE_VOTE_KNOWN_MIN hits accumulate;
// repeated hits from the same continuous face presence are suppressed until
// the face goes idle (_knownConfirmed reset). Any KNOWN hit also resets the
// sustained-UNKNOWN timer, so UNKNOWN_CONFIRMED requires 30s of UNKNOWN with
// no KNOWN hit in between. DETECTED bypasses this voter entirely.
//
// Persistent unknown visitor: UNKNOWN_CONFIRMED resets the window immediately,
// so a visitor who stays will re-trigger every FACE_VOTE_WINDOW_MS (30s).
struct FaceVoter {
  VoteResult update(FaceResult raw, unsigned long rawMs, unsigned long now);
  void reset();
  bool isActive() const { return _active; }

private:
  unsigned long _windowStartMs  = 0;
  unsigned long _unknownStartMs = 0;  // sustained-UNKNOWN timer; 0 if interrupted by KNOWN
  unsigned long _lastFaceMs     = 0;
  unsigned long _lastSampleMs   = 0;  // last processed rawMs; prevents re-sampling same frame
  unsigned long _lastProgressMs = 0;
  int           _knownCount     = 0;
  bool          _active         = false;
  bool          _knownConfirmed = false;  // suppresses repeat KNOWN_CONFIRMED until face idles
};
