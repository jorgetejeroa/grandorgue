/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiMarkersDialog.h"

#include <wx/button.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include "midi/playlist/GOMidiMarkers.h"

enum {
  ID_MARKERS_LIST = 400,
  ID_MARKERS_RENAME,
  ID_MARKERS_DELETE,
  ID_MARKERS_EARLIER,
  ID_MARKERS_LATER,
  ID_MARKERS_JUMP,
};

BEGIN_EVENT_TABLE(GOMidiMarkersDialog, wxDialog)
EVT_BUTTON(ID_MARKERS_RENAME, GOMidiMarkersDialog::OnRename)
EVT_LISTBOX_DCLICK(ID_MARKERS_LIST, GOMidiMarkersDialog::OnJump)
EVT_BUTTON(ID_MARKERS_DELETE, GOMidiMarkersDialog::OnDelete)
EVT_BUTTON(ID_MARKERS_EARLIER, GOMidiMarkersDialog::OnEarlier)
EVT_BUTTON(ID_MARKERS_LATER, GOMidiMarkersDialog::OnLater)
EVT_BUTTON(ID_MARKERS_JUMP, GOMidiMarkersDialog::OnJump)
END_EVENT_TABLE()

GOMidiMarkersDialog::GOMidiMarkersDialog(
  wxWindow *parent,
  GOMidiMarkers &markers,
  unsigned tempoPercent,
  std::function<void(double)> jump)
  : wxDialog(
      parent,
      wxID_ANY,
      _("Markers"),
      wxDefaultPosition,
      wxDefaultSize,
      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    r_markers(markers),
    m_TempoPercent(tempoPercent ? tempoPercent : 100),
    m_jump(jump) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer *mainSizer = new wxBoxSizer(wxHORIZONTAL);
  wxBoxSizer *buttonSizer = new wxBoxSizer(wxVERTICAL);

  m_List = new wxListBox(
    this, ID_MARKERS_LIST, wxDefaultPosition, wxSize(300, 220), 0, nullptr);
  mainSizer->Add(m_List, 1, wxEXPAND | wxRIGHT, 8);
  buttonSizer->Add(
    new wxButton(this, ID_MARKERS_JUMP, _("Play from here")), 0, wxEXPAND);
  buttonSizer->Add(
    new wxButton(this, ID_MARKERS_RENAME, _("Rename...")),
    0,
    wxEXPAND | wxTOP,
    4);
  buttonSizer->Add(
    new wxButton(this, ID_MARKERS_EARLIER, _("1 second earlier")),
    0,
    wxEXPAND | wxTOP,
    4);
  buttonSizer->Add(
    new wxButton(this, ID_MARKERS_LATER, _("1 second later")),
    0,
    wxEXPAND | wxTOP,
    4);
  buttonSizer->Add(
    new wxButton(this, ID_MARKERS_DELETE, _("Delete")),
    0,
    wxEXPAND | wxTOP,
    12);
  mainSizer->Add(buttonSizer, 0);
  topSizer->Add(mainSizer, 1, wxEXPAND | wxALL, 10);
  topSizer->Add(CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 10);
  SetEscapeId(wxID_CLOSE);
  SetAffirmativeId(wxID_CLOSE);
  SetSizerAndFit(topSizer);
  FillList(r_markers.GetCount() ? 0 : -1);
}

void GOMidiMarkersDialog::FillList(int selection) {
  m_List->Clear();
  for (const GOMidiMarkers::Marker &marker : r_markers.GetMarkers()) {
    const unsigned seconds
      = (unsigned)(marker.m_position * 100.0 / m_TempoPercent / 1000.0);

    m_List->Append(wxString::Format(
      wxT("%u:%02u  %s"), seconds / 60, seconds % 60, marker.m_name));
  }
  if (selection >= 0 && selection < (int)m_List->GetCount())
    m_List->SetSelection(selection);
}

int GOMidiMarkersDialog::GetSelection() const { return m_List->GetSelection(); }

void GOMidiMarkersDialog::MoveSelected(double deltaMs) {
  const int index = GetSelection();

  if (index != wxNOT_FOUND)
    FillList((int)r_markers.Move((unsigned)index, deltaMs));
}

void GOMidiMarkersDialog::OnRename(wxCommandEvent &event) {
  const int index = GetSelection();

  if (index != wxNOT_FOUND) {
    wxTextEntryDialog dlg(
      this,
      _("Name of the marker:"),
      _("Rename marker"),
      r_markers.GetMarkers()[index].m_name);

    if (dlg.ShowModal() == wxID_OK && !dlg.GetValue().Trim().IsEmpty()) {
      r_markers.Rename((unsigned)index, dlg.GetValue().Trim());
      FillList(index);
    }
  }
}

void GOMidiMarkersDialog::OnDelete(wxCommandEvent &event) {
  const int index = GetSelection();

  if (index != wxNOT_FOUND) {
    r_markers.Remove((unsigned)index);
    FillList(index < (int)r_markers.GetCount() ? index : index - 1);
  }
}

void GOMidiMarkersDialog::OnEarlier(wxCommandEvent &event) {
  MoveSelected(-1000.0 * m_TempoPercent / 100.0);
}

void GOMidiMarkersDialog::OnLater(wxCommandEvent &event) {
  MoveSelected(1000.0 * m_TempoPercent / 100.0);
}

void GOMidiMarkersDialog::OnJump(wxCommandEvent &event) {
  const int index = GetSelection();

  if (index != wxNOT_FOUND && m_jump)
    m_jump(r_markers.GetMarkers()[index].m_position);
}
