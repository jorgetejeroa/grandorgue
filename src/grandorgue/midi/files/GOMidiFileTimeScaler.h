/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIFILETIMESCALER_H
#define GOMIDIFILETIMESCALER_H

#include <cstdint>
#include <vector>

#include <wx/string.h>

/**
 * Changes the playing speed of a standard MIDI file by multiplying all event
 * times by a factor. Works for every time division (PPQ or SMPTE, the latter
 * is used by the GrandOrgue MIDI recorder), because the delta times are
 * scaled directly. The absolute times are rounded, so rounding errors do not
 * accumulate and the tracks of a type 1 file stay synchronous.
 */
class GOMidiFileTimeScaler {
public:
  /**
   * Scales the event times of MIDI file content
   * @param data the content of a MIDI file
   * @param timeFactor the factor for all times: 2 means half speed
   * @param outData the resulting MIDI file content
   * @return true on success, false if data is not a valid MIDI file
   */
  static bool scaleData(
    const std::vector<uint8_t> &data,
    double timeFactor,
    std::vector<uint8_t> &outData);

  /**
   * Reads a MIDI file, scales its event times and writes the result
   * @param inPath the file to read
   * @param outPath the file to write. May be equal to inPath
   * @param timeFactor the factor for all times: 2 means half speed
   * @return true on success
   */
  static bool scaleFile(
    const wxString &inPath, const wxString &outPath, double timeFactor);
};

#endif
