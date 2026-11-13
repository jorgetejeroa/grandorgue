/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOREMOTEDIALOG_H
#define GOREMOTEDIALOG_H

#include <wx/dialog.h>

class wxCheckBox;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

/**
 * The settings of the remote control: whether it is enabled, its port and
 * PIN. It shows the addresses to open on the phone.
 */
class GORemoteDialog : public wxDialog {
private:
  wxCheckBox *m_Enabled;
  wxSpinCtrl *m_Port;
  wxTextCtrl *m_Pin;
  wxStaticText *m_Addresses;

  void UpdateAddresses();
  void OnChanged(wxCommandEvent &event);
  void OnOk(wxCommandEvent &event);

public:
  /**
   * @param parent the parent window
   * @param isEnabled whether the remote control is enabled
   * @param port the TCP port
   * @param pin the PIN
   * @param status a line about the server state shown in the dialog
   */
  GORemoteDialog(
    wxWindow *parent,
    bool isEnabled,
    unsigned port,
    const wxString &pin,
    const wxString &status);

  bool IsRemoteEnabled() const;
  unsigned GetPort() const;
  wxString GetPin() const;

  DECLARE_EVENT_TABLE()
};

#endif
