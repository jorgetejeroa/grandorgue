/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIMARKERS_H
#define GOMIDIMARKERS_H

#include <vector>

#include <wx/string.h>

/**
 * Named positions in a MIDI file, for example the start of a verse, to jump
 * there quickly. They are stored in a small UTF-8 text file next to the MIDI
 * file, with the same name and the extension ".markers". Each line holds the
 * position in milliseconds of file time (independent of the playing tempo),
 * a tab and the name.
 */
class GOMidiMarkers {
public:
  struct Marker {
    double m_position; // milliseconds from the start of the file
    wxString m_name;
  };

private:
  std::vector<Marker> m_markers; // sorted by position

  void Sort();

public:
  /** @return the markers file belonging to a MIDI file */
  static wxString getMarkersPath(const wxString &midiPath);

  /**
   * Reads the markers of a MIDI file. No markers if there is no file
   * @param midiPath the MIDI file
   */
  void Load(const wxString &midiPath);
  /**
   * Writes the markers of a MIDI file. Removes the markers file if there are
   * no markers
   * @param midiPath the MIDI file
   * @return true on success
   */
  bool Save(const wxString &midiPath) const;

  void Clear() { m_markers.clear(); }
  const std::vector<Marker> &GetMarkers() const { return m_markers; }
  unsigned GetCount() const { return m_markers.size(); }

  /**
   * Adds a marker
   * @return the index of the new marker
   */
  unsigned Add(double position, const wxString &name);
  void Remove(unsigned index);
  void Rename(unsigned index, const wxString &name);
  /**
   * Moves a marker
   * @param index the marker
   * @param deltaMs the change of the position, may be negative
   * @return the new index of the marker
   */
  unsigned Move(unsigned index, double deltaMs);
  /**
   * Multiplies all positions, used when the file is saved with another tempo
   * @param timeFactor the factor: 2 means half speed
   */
  void Scale(double timeFactor);
};

#endif
