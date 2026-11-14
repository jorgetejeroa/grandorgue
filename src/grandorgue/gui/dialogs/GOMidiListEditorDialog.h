/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDILISTEDITORDIALOG_H
#define GOMIDILISTEDITORDIALOG_H

#include <wx/arrstr.h>
#include <wx/dialog.h>

#include "midi/playlist/GOMidiList.h"

class wxComboBox;
class wxListBox;
class wxRadioBox;
class wxStaticText;
class wxTextCtrl;

/**
 * Edits a prepared list of pieces: its name, its type (mass or free), the
 * pieces in their order and, for a mass, the moment of each piece.
 */
class GOMidiListEditorDialog : public wxDialog {
private:
  GOMidiList m_list;
  wxArrayString m_FileNames; // the pieces of the folder (file names)

  wxTextCtrl *m_Name;
  wxRadioBox *m_Type;
  wxListBox *m_Available;
  wxListBox *m_Items;
  wxStaticText *m_MomentLabel;
  wxComboBox *m_Moment;

  void FillItems(int selection);
  void UpdateControls();
  void AddPiece(int availableIndex);

  void OnType(wxCommandEvent &event);
  void OnAdd(wxCommandEvent &event);
  void OnRemove(wxCommandEvent &event);
  void OnUp(wxCommandEvent &event);
  void OnDown(wxCommandEvent &event);
  void OnItemSelected(wxCommandEvent &event);
  void OnMoment(wxCommandEvent &event);
  void OnOk(wxCommandEvent &event);

public:
  /**
   * @param parent the parent window
   * @param title the window title
   * @param name the initial name of the list
   * @param list the initial content
   * @param fileNames the MIDI file names of the playlist folder
   */
  GOMidiListEditorDialog(
    wxWindow *parent,
    const wxString &title,
    const wxString &name,
    const GOMidiList &list,
    const wxArrayString &fileNames);

  /** @return the name entered, cleaned for use as a file name */
  wxString GetName() const;
  const GOMidiList &GetList() const { return m_list; }

  DECLARE_EVENT_TABLE()
};

#endif
