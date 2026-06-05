#pragma once
#include <Arduino.h>
#include "states.h"
#include "CameraAgent.h"
#include "config.h"  // FACE_VOTE_KNOWN_MIN, FACE_VOTE_UNKNOWN_MIN_HITS

// Snapshot of FaceVoter internal state for dashboard reporting.
struct FaceVoterStatus {
  bool          active;
  bool          knownConfirmed;
  int           knownCount;         // KNOWN ring buffer fill (0–FACE_VOTE_KNOWN_MIN)
  int           knownMin;
  int           unknownHits;        // UNKNOWN ring buffer fill (0–FACE_VOTE_UNKNOWN_MIN_HITS)
  unsigned long unknownElapsedMs;   // age of oldest UNKNOWN hit in window; 0 when empty
  unsigned long unknownWindowMs;
  char          confirmedName[17];  // name at KNOWN_CONFIRMED; empty if not yet confirmed
};

// Sliding-window temporal voting for face recognition decisions.
//
// KNOWN_CONFIRMED: fires when the oldest of the last FACE_VOTE_KNOWN_MIN KNOWN
// hits is still within FACE_VOTE_KNOWN_WINDOW_MS. At 2 fps the ring buffer fills
// in ~1 s; the window check is the binding constraint (~8 s max). Repeated
// confirmation for the same continuous presence is suppressed until idle reset.
//
// UNKNOWN_CONFIRMED: fires when the oldest of the last FACE_VOTE_UNKNOWN_MIN_HITS
// UNKNOWN/DETECTED hits is still within FACE_VOTE_WINDOW_MS. At 2 fps this
// confirms after roughly (FACE_VOTE_UNKNOWN_MIN_HITS - 1) x CAMERA_DETECT_INTERVAL_MS
// (~4.5 s at default settings) — density-based, not duration-based. After
// confirmation the window resets immediately so a persistent visitor re-triggers
// every ~FACE_VOTE_UNKNOWN_MIN_HITS x CAMERA_DETECT_INTERVAL_MS worth of new hits.
//
// Before KNOWN_CONFIRMED: single KNOWN hits do not clear the UNKNOWN ring buffer to
// prevent false-known frames from suppressing unknown-visitor alerts.
// After KNOWN_CONFIRMED: KNOWN hits clear the UNKNOWN ring buffer so that the
// confirmed user's occasional recognition failures do not accumulate into a false alarm.
// UNKNOWN hits are always tracked regardless of _knownConfirmed so a stranger appearing
// after a known user still accumulates toward UNKNOWN_CONFIRMED.
struct FaceVoter {
  VoteResult      update(FaceResult raw, unsigned long rawMs, unsigned long now);
  void            reset();
  bool            isActive() const { return _active; }
  FaceVoterStatus getStatus(unsigned long now) const;
  // Call when VoteResult::KNOWN_CONFIRMED is returned, passing
  // FaceRecognizer::getLastMatchName() so the name survives later UNKNOWN frames.
  void            setConfirmedName(const char* name);

private:
  static constexpr int KNOWN_BUF   = FACE_VOTE_KNOWN_MIN;        // 3
  static constexpr int UNKNOWN_BUF = FACE_VOTE_UNKNOWN_MIN_HITS; // 10

  unsigned long _knownTs[KNOWN_BUF]     = {};  // ring buffer of recent KNOWN hit timestamps
  unsigned long _unknownTs[UNKNOWN_BUF] = {};  // ring buffer of recent UNKNOWN hit timestamps
  int           _knownHead    = 0;  // next write index (circular); oldest when buffer full
  int           _unknownHead  = 0;
  int           _knownFill    = 0;  // valid entry count (0..KNOWN_BUF)
  int           _unknownFill  = 0;

  unsigned long _lastFaceMs     = 0;
  unsigned long _lastSampleMs   = 0;  // last processed rawMs; prevents re-sampling same frame
  unsigned long _lastProgressMs = 0;
  bool          _active         = false;
  bool          _knownConfirmed = false;  // suppresses repeat KNOWN_CONFIRMED until idle reset
  char          _confirmedName[17] = {};

  // Index of the oldest entry in a circular buffer.
  // When not yet full, entries start at index 0; when full, head IS the oldest slot.
  static int _oldestIdx(int head, int fill, int buf) {
    return (fill < buf) ? 0 : head;
  }
};
