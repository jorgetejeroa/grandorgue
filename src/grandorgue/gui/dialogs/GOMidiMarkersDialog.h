/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIMARKERSDIALOG_H
#define GOMIDIMARKERSDIALOG_H

#include <functional>

#include <wx/dialog.h>

class GOMidiMarkers;
class wxListBox;

/**
 * Edits the markers of a piece: rename, delete, move by one second and jump
 * to a marker. The changes are made directly in the GOMidiMarkers object.
 */
class GOMidiMarkersDialog : public wxDialog {
private:
  GOMidiMarkers &r_markers;
  unsigned m_TempoPercent;
  std::function<void(double)> m_jump;
  wxListBox *m_List;

  void FillList(int selection);
  int GetSelection() const;
  void MoveSelected(double deltaMs);

  void OnRename(wxCommandEvent &event);
  void OnDelete(wxCommandEvent &event);
  void OnEarlier(wxCommandEvent &event);
  void OnLater(wxCommandEvent &event);
  void OnJump(wxCommandEvent &event);

public:
  /**
   * @param parent the parent window
   * @param markers the markers to edit
   * @param tempoPercent the playing tempo, to show the real times
   * @param jump plays from a position in milliseconds of file time
   */
  GOMidiMarkersDialog(
    wxWindow *parent,
    GOMidiMarkers &markers,
    unsigned tempoPercent,
    std::function<void(double)> jump);

  DECLARE_EVENT_TABLE()
};

#endif
