#pragma once
#include <Arduino.h>
#include "CameraAgent.h"

enum class VoteResult { NONE, KNOWN_CONFIRMED, UNKNOWN_CONFIRMED };

// Snapshot of FaceVoter internal state for dashboard reporting.
struct FaceVoterStatus {
  bool          active;
  bool          knownConfirmed;
  int           knownCount;
  int           knownMin;
  int           unknownHits;
  unsigned long unknownElapsedMs;   // 0 when no UNKNOWN tracking active
  unsigned long unknownWindowMs;
  char          confirmedName[17];  // name at KNOWN_CONFIRMED; empty if not yet confirmed
};

// Temporal voting window for face recognition decisions.
//
// KNOWN_CONFIRMED: requires FACE_VOTE_KNOWN_MIN hits accumulated within
// FACE_VOTE_KNOWN_WINDOW_MS. Burst window resets if it expires without
// enough hits. Repeated KNOWN_CONFIRMED for the same continuous presence
// is suppressed until the face idles out and reset() is called.
//
// UNKNOWN_CONFIRMED: requires BOTH FACE_VOTE_WINDOW_MS elapsed since first
// UNKNOWN AND at least FACE_VOTE_UNKNOWN_MIN_HITS frame hits. Only
// KNOWN_CONFIRMED clears the UNKNOWN timer — single KNOWN hits do not, to
// avoid false-known events suppressing unknown-visitor alerts.
//
// Persistent unknown visitor: UNKNOWN_CONFIRMED resets the window immediately,
// so a visitor who stays will re-trigger every FACE_VOTE_WINDOW_MS (30s).
struct FaceVoter {
  VoteResult      update(FaceResult raw, unsigned long rawMs, unsigned long now);
  void            reset();
  bool            isActive() const { return _active; }
  FaceVoterStatus getStatus(unsigned long now) const;
  // Call from outdoor_main when VoteResult::KNOWN_CONFIRMED is returned, passing
  // FaceRecognizer::getLastMatchName() so the name is preserved through later UNKNOWN frames.
  void            setConfirmedName(const char* name);

private:
  unsigned long _windowStartMs  = 0;
  unsigned long _firstKnownMs   = 0;  // start of current KNOWN burst window
  unsigned long _unknownStartMs = 0;  // sustained-UNKNOWN timer; 0 until first UNKNOWN
  unsigned long _lastFaceMs     = 0;
  unsigned long _lastSampleMs   = 0;  // last processed rawMs; prevents re-sampling same frame
  unsigned long _lastProgressMs = 0;
  int           _knownCount     = 0;  // KNOWN hits accumulated in current burst
  int           _unknownHits    = 0;  // UNKNOWN frame count since _unknownStartMs
  bool          _active         = false;
  bool          _knownConfirmed = false;  // suppresses repeat KNOWN_CONFIRMED until face idles
  char          _confirmedName[17] = {};  // name captured at KNOWN_CONFIRMED
};
