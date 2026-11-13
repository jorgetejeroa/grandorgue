/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GORemoteDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>

#include "remote/GORemoteServer.h"

enum {
  ID_REMOTE_ENABLED = 300,
  ID_REMOTE_PORT,
  ID_REMOTE_PIN,
};

BEGIN_EVENT_TABLE(GORemoteDialog, wxDialog)
EVT_CHECKBOX(ID_REMOTE_ENABLED, GORemoteDialog::OnChanged)
EVT_TEXT(ID_REMOTE_PORT, GORemoteDialog::OnChanged)
EVT_TEXT(ID_REMOTE_PIN, GORemoteDialog::OnChanged)
EVT_BUTTON(wxID_OK, GORemoteDialog::OnOk)
END_EVENT_TABLE()

GORemoteDialog::GORemoteDialog(
  wxWindow *parent,
  bool isEnabled,
  unsigned port,
  const wxString &pin,
  const wxString &status)
  : wxDialog(parent, wxID_ANY, _("Remote control")),
    m_Enabled(nullptr),
    m_Port(nullptr),
    m_Pin(nullptr),
    m_Addresses(nullptr) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *intro = new wxStaticText(
    this,
    wxID_ANY,
    _("Control the MIDI playlist, the volume and the combinations from a "
      "phone or a tablet connected to the same Wi-Fi network as this "
      "computer."));

  intro->Wrap(420);
  topSizer->Add(intro, 0, wxALL, 10);

  m_Enabled
    = new wxCheckBox(this, ID_REMOTE_ENABLED, _("Enable the remote control"));
  m_Enabled->SetValue(isEnabled);
  topSizer->Add(m_Enabled, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

  wxFlexGridSizer *grid = new wxFlexGridSizer(2, 8, 8);

  grid->Add(
    new wxStaticText(this, wxID_ANY, _("PIN:")), 0, wxALIGN_CENTER_VERTICAL);
  m_Pin = new wxTextCtrl(
    this,
    ID_REMOTE_PIN,
    pin,
    wxDefaultPosition,
    wxSize(120, -1),
    0,
    wxTextValidator(wxFILTER_DIGITS));
  m_Pin->SetMaxLength(8);
  grid->Add(m_Pin);
  grid->Add(
    new wxStaticText(this, wxID_ANY, _("Port:")), 0, wxALIGN_CENTER_VERTICAL);
  m_Port = new wxSpinCtrl(
    this,
    ID_REMOTE_PORT,
    wxEmptyString,
    wxDefaultPosition,
    wxSize(120, -1),
    wxSP_ARROW_KEYS,
    1024,
    65535,
    port);
  grid->Add(m_Port);
  topSizer->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

  m_Addresses = new wxStaticText(this, wxID_ANY, wxEmptyString);
  wxFont font = m_Addresses->GetFont();

  font.SetPointSize(font.GetPointSize() + 2);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  m_Addresses->SetFont(font);
  topSizer->Add(
    new wxStaticText(
      this, wxID_ANY, _("On the phone, open this address in the browser:")),
    0,
    wxLEFT | wxRIGHT,
    10);
  topSizer->Add(m_Addresses, 0, wxALL, 10);

  if (!status.IsEmpty())
    topSizer->Add(
      new wxStaticText(this, wxID_ANY, status), 0, wxLEFT | wxRIGHT, 10);

  wxStaticText *hint = new wxStaticText(
    this,
    wxID_ANY,
    _("The first time, Windows may ask whether GrandOrgue may use the "
      "network: allow it. In the browser of the phone, the page can be added "
      "to the home screen to open it like an app."));

  hint->Wrap(420);
  topSizer->Add(hint, 0, wxALL, 10);
  topSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
  SetSizerAndFit(topSizer);
  UpdateAddresses();
}

void GORemoteDialog::UpdateAddresses() {
  const std::vector<wxString> addresses = GORemoteServer::GetLocalAddresses();
  wxString text;

  for (const wxString &address : addresses) {
    if (!text.IsEmpty())
      text += wxT("\n");
    text += wxString::Format(wxT("http://%s:%d"), address, m_Port->GetValue());
  }
  if (text.IsEmpty())
    text = wxString::Format(
      _("http://<address of this computer>:%d"), m_Port->GetValue());
  m_Addresses->SetLabel(text);
  m_Port->Enable(m_Enabled->GetValue());
  m_Pin->Enable(m_Enabled->GetValue());
  Layout();
  Fit();
}

void GORemoteDialog::OnChanged(wxCommandEvent &event) {
  // the controls may send events while the dialog is being built
  if (m_Enabled && m_Port && m_Pin && m_Addresses)
    UpdateAddresses();
}

void GORemoteDialog::OnOk(wxCommandEvent &event) {
  if (m_Enabled->GetValue() && GetPin().Length() < 4)
    wxMessageBox(
      _("Please enter a PIN of at least 4 digits."),
      _("Remote control"),
      wxOK | wxICON_WARNING,
      this);
  else
    EndModal(wxID_OK);
}

bool GORemoteDialog::IsRemoteEnabled() const { return m_Enabled->GetValue(); }

unsigned GORemoteDialog::GetPort() const { return m_Port->GetValue(); }

wxString GORemoteDialog::GetPin() const { return m_Pin->GetValue().Trim(); }
