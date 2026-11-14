/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiListEditorDialog.h"

#include <utility>

#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

enum {
  ID_LISTED_TYPE = 500,
  ID_LISTED_AVAILABLE,
  ID_LISTED_ADD,
  ID_LISTED_ITEMS,
  ID_LISTED_REMOVE,
  ID_LISTED_UP,
  ID_LISTED_DOWN,
  ID_LISTED_MOMENT,
};

BEGIN_EVENT_TABLE(GOMidiListEditorDialog, wxDialog)
EVT_RADIOBOX(ID_LISTED_TYPE, GOMidiListEditorDialog::OnType)
EVT_BUTTON(ID_LISTED_ADD, GOMidiListEditorDialog::OnAdd)
EVT_LISTBOX_DCLICK(ID_LISTED_AVAILABLE, GOMidiListEditorDialog::OnAdd)
EVT_BUTTON(ID_LISTED_REMOVE, GOMidiListEditorDialog::OnRemove)
EVT_BUTTON(ID_LISTED_UP, GOMidiListEditorDialog::OnUp)
EVT_BUTTON(ID_LISTED_DOWN, GOMidiListEditorDialog::OnDown)
EVT_LISTBOX(ID_LISTED_ITEMS, GOMidiListEditorDialog::OnItemSelected)
EVT_COMBOBOX(ID_LISTED_MOMENT, GOMidiListEditorDialog::OnMoment)
EVT_TEXT(ID_LISTED_MOMENT, GOMidiListEditorDialog::OnMoment)
EVT_BUTTON(wxID_OK, GOMidiListEditorDialog::OnOk)
END_EVENT_TABLE()

GOMidiListEditorDialog::GOMidiListEditorDialog(
  wxWindow *parent,
  const wxString &title,
  const wxString &name,
  const GOMidiList &list,
  const wxArrayString &fileNames)
  : wxDialog(
      parent,
      wxID_ANY,
      title,
      wxDefaultPosition,
      wxDefaultSize,
      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_list(list),
    m_FileNames(fileNames),
    m_Name(nullptr),
    m_Type(nullptr),
    m_Available(nullptr),
    m_Items(nullptr),
    m_MomentLabel(nullptr),
    m_Moment(nullptr) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

  // the name and the type
  wxBoxSizer *nameSizer = new wxBoxSizer(wxHORIZONTAL);
  nameSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Name:")),
    0,
    wxALIGN_CENTER_VERTICAL | wxRIGHT,
    6);
  m_Name = new wxTextCtrl(this, wxID_ANY, name);
  nameSizer->Add(m_Name, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

  wxString types[] = {_("Mass"), _("Free")};

  m_Type = new wxRadioBox(
    this,
    ID_LISTED_TYPE,
    _("Type"),
    wxDefaultPosition,
    wxDefaultSize,
    2,
    types,
    1,
    wxRA_SPECIFY_ROWS);
  m_Type->SetSelection(m_list.IsMass() ? 0 : 1);
  nameSizer->Add(m_Type, 0, wxALIGN_CENTER_VERTICAL);
  topSizer->Add(nameSizer, 0, wxEXPAND | wxALL, 10);

  // the pieces of the folder and the pieces of the list
  wxBoxSizer *listsSizer = new wxBoxSizer(wxHORIZONTAL);
  wxBoxSizer *leftSizer = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer *rightSizer = new wxBoxSizer(wxVERTICAL);

  leftSizer->Add(new wxStaticText(this, wxID_ANY, _("All pieces:")));
  m_Available = new wxListBox(
    this, ID_LISTED_AVAILABLE, wxDefaultPosition, wxSize(240, 300));
  for (const wxString &fileName : m_FileNames)
    m_Available->Append(wxFileName(fileName).GetName());
  leftSizer->Add(m_Available, 1, wxEXPAND | wxTOP, 4);
  listsSizer->Add(leftSizer, 1, wxEXPAND | wxRIGHT, 8);

  listsSizer->Add(
    new wxButton(this, ID_LISTED_ADD, _("Add >>")),
    0,
    wxALIGN_CENTER_VERTICAL | wxRIGHT,
    8);

  rightSizer->Add(new wxStaticText(this, wxID_ANY, _("In this list:")));
  m_Items
    = new wxListBox(this, ID_LISTED_ITEMS, wxDefaultPosition, wxSize(300, 300));
  rightSizer->Add(m_Items, 1, wxEXPAND | wxTOP, 4);

  wxBoxSizer *itemButtons = new wxBoxSizer(wxHORIZONTAL);

  itemButtons->Add(new wxButton(this, ID_LISTED_UP, _("Up")), 1, wxRIGHT, 4);
  itemButtons->Add(
    new wxButton(this, ID_LISTED_DOWN, _("Down")), 1, wxRIGHT, 4);
  itemButtons->Add(new wxButton(this, ID_LISTED_REMOVE, _("Remove")), 1);
  rightSizer->Add(itemButtons, 0, wxEXPAND | wxTOP, 6);

  wxBoxSizer *momentSizer = new wxBoxSizer(wxHORIZONTAL);

  m_MomentLabel = new wxStaticText(this, wxID_ANY, _("Moment:"));
  momentSizer->Add(m_MomentLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

  wxArrayString moments = GOMidiList::getMassMoments();

  moments.Insert(wxEmptyString, 0);
  m_Moment = new wxComboBox(
    this,
    ID_LISTED_MOMENT,
    wxEmptyString,
    wxDefaultPosition,
    wxDefaultSize,
    moments,
    wxCB_DROPDOWN);
  momentSizer->Add(m_Moment, 1);
  rightSizer->Add(momentSizer, 0, wxEXPAND | wxTOP, 6);
  listsSizer->Add(rightSizer, 1, wxEXPAND);
  topSizer->Add(listsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

  topSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
  SetSizerAndFit(topSizer);
  FillItems(m_list.GetItems().empty() ? -1 : 0);
}

void GOMidiListEditorDialog::FillItems(int selection) {
  const bool isMass = m_Type->GetSelection() == 0;

  m_Items->Clear();
  for (const GOMidiList::Item &item : m_list.GetItems()) {
    const wxString name = wxFileName(item.m_FileName).GetName();

    m_Items->Append(
      isMass && !item.m_moment.IsEmpty() ? item.m_moment + wxT("  -  ") + name
                                         : name);
  }
  if (selection >= 0 && selection < (int)m_Items->GetCount())
    m_Items->SetSelection(selection);
  UpdateControls();
}

void GOMidiListEditorDialog::UpdateControls() {
  const bool isMass = m_Type->GetSelection() == 0;
  const int selection = m_Items->GetSelection();

  m_MomentLabel->Show(isMass);
  m_Moment->Show(isMass);
  m_Moment->Enable(selection != wxNOT_FOUND);
  if (selection != wxNOT_FOUND)
    m_Moment->ChangeValue(m_list.GetItems()[selection].m_moment);
  else
    m_Moment->ChangeValue(wxEmptyString);
  Layout();
}

void GOMidiListEditorDialog::AddPiece(int availableIndex) {
  if (availableIndex >= 0 && availableIndex < (int)m_FileNames.GetCount()) {
    std::vector<GOMidiList::Item> &items = m_list.GetItems();
    const int selection = m_Items->GetSelection();
    // insert after the selected piece, else at the end
    const unsigned position
      = selection != wxNOT_FOUND ? (unsigned)selection + 1 : items.size();
    const wxString previousMoment
      = position > 0 ? items[position - 1].m_moment : wxString();
    GOMidiList::Item item{
      m_Type->GetSelection() == 0
        ? GOMidiList::proposeNextMoment(previousMoment)
        : wxString(),
      m_FileNames[availableIndex]};

    items.insert(items.begin() + position, item);
    FillItems((int)position);
  }
}

void GOMidiListEditorDialog::OnType(wxCommandEvent &event) {
  m_list.SetMass(m_Type->GetSelection() == 0);
  FillItems(m_Items->GetSelection());
}

void GOMidiListEditorDialog::OnAdd(wxCommandEvent &event) {
  AddPiece(m_Available->GetSelection());
}

void GOMidiListEditorDialog::OnRemove(wxCommandEvent &event) {
  std::vector<GOMidiList::Item> &items = m_list.GetItems();
  const int selection = m_Items->GetSelection();

  if (selection != wxNOT_FOUND) {
    items.erase(items.begin() + selection);
    FillItems(selection < (int)items.size() ? selection : selection - 1);
  }
}

void GOMidiListEditorDialog::OnUp(wxCommandEvent &event) {
  std::vector<GOMidiList::Item> &items = m_list.GetItems();
  const int selection = m_Items->GetSelection();

  if (selection > 0) {
    std::swap(items[selection], items[selection - 1]);
    FillItems(selection - 1);
  }
}

void GOMidiListEditorDialog::OnDown(wxCommandEvent &event) {
  std::vector<GOMidiList::Item> &items = m_list.GetItems();
  const int selection = m_Items->GetSelection();

  if (selection != wxNOT_FOUND && selection + 1 < (int)items.size()) {
    std::swap(items[selection], items[selection + 1]);
    FillItems(selection + 1);
  }
}

void GOMidiListEditorDialog::OnItemSelected(wxCommandEvent &event) {
  UpdateControls();
}

void GOMidiListEditorDialog::OnMoment(wxCommandEvent &event) {
  const int selection = m_Items ? m_Items->GetSelection() : wxNOT_FOUND;

  if (selection != wxNOT_FOUND) {
    GOMidiList::Item &item = m_list.GetItems()[selection];
    const wxString moment = m_Moment->GetValue().Trim().Trim(false);

    if (item.m_moment != moment) {
      item.m_moment = moment;
      m_Items->SetString(
        selection,
        moment.IsEmpty()
          ? wxFileName(item.m_FileName).GetName()
          : moment + wxT("  -  ") + wxFileName(item.m_FileName).GetName());
    }
  }
}

void GOMidiListEditorDialog::OnOk(wxCommandEvent &event) {
  if (GetName().IsEmpty())
    wxMessageBox(
      _("Please enter a name for the list."),
      GetTitle(),
      wxOK | wxICON_WARNING,
      this);
  else {
    m_list.SetMass(m_Type->GetSelection() == 0);
    EndModal(wxID_OK);
  }
}

wxString GOMidiListEditorDialog::GetName() const {
  return GOMidiList::cleanName(m_Name->GetValue());
}
