/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiPlaylistDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

#include "config/GOConfig.h"
#include "midi/GOMidiPlayer.h"
#include "midi/files/GOMidiFileTimeScaler.h"

enum {
  ID_PLAYLIST_FOLDER = 200,
  ID_PLAYLIST_REFRESH,
  ID_PLAYLIST_LIST,
  ID_PLAYLIST_PREV,
  ID_PLAYLIST_PLAY,
  ID_PLAYLIST_PAUSE,
  ID_PLAYLIST_STOP,
  ID_PLAYLIST_NEXT,
  ID_PLAYLIST_SLOWER,
  ID_PLAYLIST_TEMPO_RESET,
  ID_PLAYLIST_FASTER,
  ID_PLAYLIST_AUTO_ADVANCE,
  ID_PLAYLIST_TIMER,
  ID_PLAYLIST_SAVE_TEMPO,
  ID_PLAYLIST_TIMELINE,
};

// the timeline resolution
static const int TIMELINE_STEPS = 1000;

BEGIN_EVENT_TABLE(GOMidiPlaylistDialog, wxDialog)
EVT_BUTTON(ID_PLAYLIST_FOLDER, GOMidiPlaylistDialog::OnChooseFolder)
EVT_BUTTON(ID_PLAYLIST_REFRESH, GOMidiPlaylistDialog::OnRefreshList)
EVT_LISTBOX(ID_PLAYLIST_LIST, GOMidiPlaylistDialog::OnListSelect)
EVT_LISTBOX_DCLICK(ID_PLAYLIST_LIST, GOMidiPlaylistDialog::OnListDoubleClick)
EVT_BUTTON(ID_PLAYLIST_PREV, GOMidiPlaylistDialog::OnPrev)
EVT_BUTTON(ID_PLAYLIST_PLAY, GOMidiPlaylistDialog::OnPlay)
EVT_BUTTON(ID_PLAYLIST_PAUSE, GOMidiPlaylistDialog::OnPause)
EVT_BUTTON(ID_PLAYLIST_STOP, GOMidiPlaylistDialog::OnStop)
EVT_BUTTON(ID_PLAYLIST_NEXT, GOMidiPlaylistDialog::OnNext)
EVT_BUTTON(ID_PLAYLIST_SLOWER, GOMidiPlaylistDialog::OnSlower)
EVT_BUTTON(ID_PLAYLIST_TEMPO_RESET, GOMidiPlaylistDialog::OnTempoReset)
EVT_BUTTON(ID_PLAYLIST_FASTER, GOMidiPlaylistDialog::OnFaster)
EVT_CHECKBOX(ID_PLAYLIST_AUTO_ADVANCE, GOMidiPlaylistDialog::OnAutoAdvance)
EVT_TIMER(ID_PLAYLIST_TIMER, GOMidiPlaylistDialog::OnRefreshTimer)
EVT_BUTTON(ID_PLAYLIST_SAVE_TEMPO, GOMidiPlaylistDialog::OnSaveTempo)
EVT_COMMAND_SCROLL_THUMBTRACK(
  ID_PLAYLIST_TIMELINE, GOMidiPlaylistDialog::OnTimelineTrack)
EVT_COMMAND_SCROLL_THUMBRELEASE(
  ID_PLAYLIST_TIMELINE, GOMidiPlaylistDialog::OnTimelineRelease)
EVT_COMMAND_SCROLL_CHANGED(
  ID_PLAYLIST_TIMELINE, GOMidiPlaylistDialog::OnTimelineRelease)
EVT_CLOSE(GOMidiPlaylistDialog::OnClose)
END_EVENT_TABLE()

static int compare_file_names(const wxString &a, const wxString &b) {
  return wxFileName(a).GetFullName().CmpNoCase(wxFileName(b).GetFullName());
}

GOMidiPlaylistDialog::GOMidiPlaylistDialog(
  wxWindow *parent, GOConfig &config, Host &host)
  : wxDialog(
      parent,
      wxID_ANY,
      _("MIDI playlist"),
      wxDefaultPosition,
      wxDefaultSize,
      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    r_config(config),
    r_host(host),
    m_IsSeeking(false),
    m_LastSeekValue(-1),
    m_LastSeekMs(0),
    m_RefreshTimer(this, ID_PLAYLIST_TIMER) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

  // the folder
  wxBoxSizer *folderSizer = new wxBoxSizer(wxHORIZONTAL);
  folderSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Folder:")),
    0,
    wxALIGN_CENTER_VERTICAL | wxRIGHT,
    5);
  m_FolderText = new wxStaticText(
    this,
    wxID_ANY,
    wxEmptyString,
    wxDefaultPosition,
    wxDefaultSize,
    wxST_ELLIPSIZE_MIDDLE);
  folderSizer->Add(m_FolderText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  folderSizer->Add(
    new wxButton(this, ID_PLAYLIST_FOLDER, _("Change folder...")),
    0,
    wxRIGHT,
    5);
  folderSizer->Add(new wxButton(this, ID_PLAYLIST_REFRESH, _("Refresh")));
  topSizer->Add(folderSizer, 0, wxEXPAND | wxALL, 8);

  // the list of files
  m_List = new wxListBox(
    this,
    ID_PLAYLIST_LIST,
    wxDefaultPosition,
    wxSize(420, 300),
    0,
    nullptr,
    wxLB_SINGLE | wxLB_NEEDED_SB);
  topSizer->Add(m_List, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // the timeline
  wxBoxSizer *timelineSizer = new wxBoxSizer(wxHORIZONTAL);
  m_Timeline = new wxSlider(
    this,
    ID_PLAYLIST_TIMELINE,
    0,
    0,
    TIMELINE_STEPS,
    wxDefaultPosition,
    wxDefaultSize,
    wxSL_HORIZONTAL);
  m_Timeline->SetPageSize(TIMELINE_STEPS / 20);
  m_TimeText = new wxStaticText(
    this,
    wxID_ANY,
    wxT("0:00 / 0:00"),
    wxDefaultPosition,
    wxSize(110, -1),
    wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
  timelineSizer->Add(m_Timeline, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  timelineSizer->Add(m_TimeText, 0, wxALIGN_CENTER_VERTICAL);
  topSizer->Add(timelineSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  // the transport buttons
  wxBoxSizer *transportSizer = new wxBoxSizer(wxHORIZONTAL);
  m_PrevButton = new wxButton(this, ID_PLAYLIST_PREV, _("<< Previous"));
  m_PlayButton = new wxButton(this, ID_PLAYLIST_PLAY, _("Play"));
  m_PauseButton = new wxButton(this, ID_PLAYLIST_PAUSE, _("Pause"));
  m_StopButton = new wxButton(this, ID_PLAYLIST_STOP, _("Stop playing"));
  m_NextButton = new wxButton(this, ID_PLAYLIST_NEXT, _("Next >>"));
  transportSizer->Add(m_PrevButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_PlayButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_PauseButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_StopButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_NextButton, 1);
  topSizer->Add(transportSizer, 0, wxEXPAND | wxALL, 8);

  // the tempo
  wxBoxSizer *tempoSizer = new wxBoxSizer(wxHORIZONTAL);
  tempoSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Tempo:")),
    0,
    wxALIGN_CENTER_VERTICAL | wxRIGHT,
    8);
  tempoSizer->Add(
    new wxButton(
      this, ID_PLAYLIST_SLOWER, wxT("-"), wxDefaultPosition, wxSize(40, -1)),
    0,
    wxRIGHT,
    4);
  m_TempoText = new wxStaticText(
    this,
    wxID_ANY,
    wxT("100%"),
    wxDefaultPosition,
    wxSize(60, -1),
    wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
  tempoSizer->Add(m_TempoText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  tempoSizer->Add(
    new wxButton(
      this, ID_PLAYLIST_FASTER, wxT("+"), wxDefaultPosition, wxSize(40, -1)),
    0,
    wxRIGHT,
    12);
  tempoSizer->Add(
    new wxButton(this, ID_PLAYLIST_TEMPO_RESET, _("Normal tempo (100%)")),
    0,
    wxRIGHT,
    12);
  m_SaveTempoButton
    = new wxButton(this, ID_PLAYLIST_SAVE_TEMPO, _("Save at this tempo"));
  tempoSizer->Add(m_SaveTempoButton);
  topSizer->Add(tempoSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  // the automatic advance switch
  m_AutoAdvance = new wxCheckBox(
    this, ID_PLAYLIST_AUTO_ADVANCE, _("Go on to the next piece automatically"));
  m_AutoAdvance->SetValue(r_config.MidiPlaylistAutoAdvance());
  topSizer->Add(m_AutoAdvance, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  // the status line
  m_StatusText = new wxStaticText(
    this,
    wxID_ANY,
    wxEmptyString,
    wxDefaultPosition,
    wxDefaultSize,
    wxST_ELLIPSIZE_END);
  topSizer->Add(m_StatusText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  SetSizerAndFit(topSizer);
  FillList();
  UpdateState();
}

GOMidiPlaylistDialog::~GOMidiPlaylistDialog() { m_RefreshTimer.Stop(); }

void GOMidiPlaylistDialog::FillList() {
  const wxString folder = r_config.MidiPlaylistPath();
  const wxString selected = GetSelectedPath();

  m_FolderText->SetLabel(folder);
  m_FolderText->SetToolTip(folder);
  m_Files.Clear();
  m_List->Clear();
  if (wxDir::Exists(folder)) {
    wxDir::GetAllFiles(folder, &m_Files, wxT("*.mid"), wxDIR_FILES);
    m_Files.Sort(compare_file_names);
  }
  for (const wxString &path : m_Files)
    m_List->Append(wxFileName(path).GetName());

  // keep the selection if possible
  int index = m_Files.Index(selected);

  if (index == wxNOT_FOUND && !m_LoadedPath.IsEmpty())
    index = m_Files.Index(m_LoadedPath);
  if (index != wxNOT_FOUND)
    m_List->SetSelection(index);
}

wxString GOMidiPlaylistDialog::GetSelectedPath() const {
  const int index = m_List->GetSelection();

  return index != wxNOT_FOUND && index < (int)m_Files.GetCount()
    ? m_Files[index]
    : wxString();
}

bool GOMidiPlaylistDialog::IsLoadedByMe(const wxString &path) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  return player && player->IsLoaded() && !m_LoadedPath.IsEmpty()
    && m_LoadedPath == path;
}

bool GOMidiPlaylistDialog::LoadIndex(int index) {
  bool isLoaded = false;

  if (index >= 0 && index < (int)m_Files.GetCount()) {
    const wxString path = m_Files[index];
    // the player may call the function when the organ is already replaced,
    // so it must not use the dialog if it has been destroyed
    wxWeakRef<GOMidiPlaylistDialog> self(this);

    m_List->SetSelection(index);
    m_LoadedPath = wxEmptyString;
    isLoaded = r_host.LoadMidiFile(path, [self]() {
      if (self)
        // do not change the player from inside its own timer handler
        self->CallAfter(&GOMidiPlaylistDialog::OnFileFinished);
    });
    if (isLoaded)
      m_LoadedPath = path;
  }
  UpdateState();
  return isLoaded;
}

void GOMidiPlaylistDialog::PlayIndex(int index) {
  if (
    index >= 0 && index < (int)m_Files.GetCount()
    && (IsLoadedByMe(m_Files[index]) || LoadIndex(index))) {
    GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

    if (pPlayer)
      pPlayer->Play();
  }
  UpdateState();
}

void GOMidiPlaylistDialog::OnFileFinished() {
  const int current = m_Files.Index(m_LoadedPath);
  const int selected = m_List->GetSelection();
  int next = current == wxNOT_FOUND ? wxNOT_FOUND : current + 1;

  // a piece chosen while the current one was playing goes first
  if (selected != wxNOT_FOUND && selected != current)
    next = selected;

  if (next != wxNOT_FOUND && next < (int)m_Files.GetCount()) {
    if (m_AutoAdvance->GetValue())
      PlayIndex(next);
    else
      // prepare the next piece so that only Play has to be pressed
      LoadIndex(next);
  }
  UpdateState();
}

void GOMidiPlaylistDialog::ChangeTempo(int delta) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  if (player) {
    const int tempo = (int)player->GetTempoPercent() + delta;

    player->SetTempoPercent(tempo > 0 ? (unsigned)tempo : 1);
  }
  UpdateState();
}

void GOMidiPlaylistDialog::UpdateState() {
  GOMidiPlayer *player = r_host.GetMidiPlayer();
  const bool hasFiles = !m_Files.IsEmpty();
  const int selected = m_List->GetSelection();

  // the player content may have been replaced (other organ, Load MIDI menu)
  if (!player || !player->IsLoaded())
    m_LoadedPath = wxEmptyString;

  m_PrevButton->Enable(player && hasFiles && selected > 0);
  m_NextButton->Enable(
    player && hasFiles && selected != wxNOT_FOUND
    && selected + 1 < (int)m_Files.GetCount());
  m_PlayButton->Enable(player && hasFiles && selected != wxNOT_FOUND);
  m_PauseButton->Enable(player && player->IsPlaying());
  m_PauseButton->SetLabel(
    player && player->IsPaused() ? _("Continue") : _("Pause"));
  m_StopButton->Enable(player && player->IsPlaying());

  m_TempoText->SetLabel(
    wxString::Format(wxT("%u%%"), player ? player->GetTempoPercent() : 100u));

  wxString status;

  if (!player)
    status = _("No organ is loaded");
  else if (!hasFiles)
    status = _("There are no MIDI files in this folder");
  else if (m_LoadedPath.IsEmpty())
    status = _("Click on a piece to load it");
  else {
    const wxString name = wxFileName(m_LoadedPath).GetName();

    if (player->IsPaused())
      status = wxString::Format(_("Paused: %s"), name);
    else if (player->IsPlaying())
      status = wxString::Format(_("Playing: %s"), name);
    if (player->IsPlaying() && GetSelectedPath() != m_LoadedPath)
      status += wxT("  -  ")
        + wxString::Format(
                  _("Next: %s"), wxFileName(GetSelectedPath()).GetName());
    else
      status = wxString::Format(_("Ready to play: %s"), name);
  }
  if (m_StatusText->GetLabel() != status)
    m_StatusText->SetLabel(status);

  m_SaveTempoButton->Enable(
    player && !m_LoadedPath.IsEmpty() && !player->IsPlaying()
    && player->GetTempoPercent() != 100);
  UpdateTimeline();
}

wxString GOMidiPlaylistDialog::FormatTime(double fileTimeMs) const {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const unsigned tempo = pPlayer ? pPlayer->GetTempoPercent() : 100;
  const unsigned seconds = (unsigned)(fileTimeMs * 100.0 / tempo / 1000.0);

  return wxString::Format(wxT("%u:%02u"), seconds / 60, seconds % 60);
}

void GOMidiPlaylistDialog::UpdateTimeline() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const bool hasFile = pPlayer && !m_LoadedPath.IsEmpty();
  const double lengthMs = hasFile ? pPlayer->GetLength().ToDouble() : 0;

  m_Timeline->Enable(hasFile && lengthMs > 0);
  if (!m_IsSeeking) {
    const double positionMs = hasFile ? pPlayer->GetPosition() : 0;
    const int value
      = lengthMs > 0 ? (int)(positionMs / lengthMs * TIMELINE_STEPS) : 0;

    if (m_Timeline->GetValue() != value)
      m_Timeline->SetValue(value);

    const wxString text = wxString::Format(
      wxT("%s / %s"), FormatTime(positionMs), FormatTime(lengthMs));

    if (m_TimeText->GetLabel() != text)
      m_TimeText->SetLabel(text);
  }
}

void GOMidiPlaylistDialog::SeekToTimeline() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const int value = m_Timeline->GetValue();
  const GOTime nowMs = wxGetLocalTimeMillis();

  // wx may report one movement twice (thumb release and changed)
  if (
    pPlayer && !m_LoadedPath.IsEmpty()
    && !(value == m_LastSeekValue && nowMs - m_LastSeekMs < 500)) {
    const double lengthMs = pPlayer->GetLength().ToDouble();

    m_LastSeekValue = value;
    m_LastSeekMs = nowMs;
    pPlayer->SeekTo(lengthMs * value / TIMELINE_STEPS);
  }
  m_IsSeeking = false;
  UpdateState();
}

void GOMidiPlaylistDialog::OnTimelineTrack(wxScrollEvent &event) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  // while dragging, only show the time of the thumb position
  m_IsSeeking = true;
  if (pPlayer) {
    const double lengthMs = pPlayer->GetLength().ToDouble();

    m_TimeText->SetLabel(wxString::Format(
      wxT("%s / %s"),
      FormatTime(lengthMs * m_Timeline->GetValue() / TIMELINE_STEPS),
      FormatTime(lengthMs)));
  }
}

void GOMidiPlaylistDialog::OnTimelineRelease(wxScrollEvent &event) {
  SeekToTimeline();
}

void GOMidiPlaylistDialog::OnSaveTempo(wxCommandEvent &event) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer && !m_LoadedPath.IsEmpty()) {
    const wxString path = m_LoadedPath;
    const wxFileName fileName(path);
    const unsigned tempo = pPlayer->GetTempoPercent();
    const wxString backupDirName = _("Originals");
    const wxString backupDir
      = fileName.GetPath() + wxFileName::GetPathSeparator() + backupDirName;
    const wxString backupPath
      = backupDir + wxFileName::GetPathSeparator() + fileName.GetFullName();

    if (
      wxMessageBox(
        wxString::Format(
          _("The piece \"%s\" will be saved with the tempo %u%%.\n\n"
            "A copy of the original file is kept in the subfolder \"%s\" "
            "(only the first time, so it is always the original "
            "recording).\n\nContinue?"),
          fileName.GetName(),
          tempo,
          backupDirName),
        _("Save at this tempo"),
        wxYES_NO | wxICON_QUESTION,
        this)
      == wxYES) {
      bool isOk = wxDirExists(backupDir) || wxMkdir(backupDir);

      if (isOk && !wxFileExists(backupPath))
        isOk = wxCopyFile(path, backupPath, false);
      if (!isOk)
        wxMessageBox(
          wxString::Format(
            _("Could not make the copy of the original file in \"%s\". "
              "Nothing has been changed."),
            backupDir),
          _("Save at this tempo"),
          wxOK | wxICON_ERROR,
          this);
      else if (!GOMidiFileTimeScaler::scaleFile(path, path, 100.0 / tempo))
        wxMessageBox(
          _("Could not save the piece. The file has not been changed."),
          _("Save at this tempo"),
          wxOK | wxICON_ERROR,
          this);
      else {
        // the file now has the new tempo: play it at 100% from now on
        pPlayer->SetTempoPercent(100);
        LoadIndex(m_Files.Index(path));
      }
    }
  }
  UpdateState();
}

void GOMidiPlaylistDialog::ForgetLoadedFile() {
  m_LoadedPath = wxEmptyString;
  UpdateState();
}

void GOMidiPlaylistDialog::OnChooseFolder(wxCommandEvent &event) {
  wxDirDialog dlg(
    this,
    _("Choose the folder with the MIDI files"),
    r_config.MidiPlaylistPath(),
    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

  if (dlg.ShowModal() == wxID_OK) {
    r_config.MidiPlaylistPath(dlg.GetPath());
    r_config.Flush();
    FillList();
    UpdateState();
  }
}

void GOMidiPlaylistDialog::OnRefreshList(wxCommandEvent &event) {
  FillList();
  UpdateState();
}

void GOMidiPlaylistDialog::OnListSelect(wxCommandEvent &event) {
  const int index = m_List->GetSelection();

  GOMidiPlayer *player = r_host.GetMidiPlayer();

  // While a piece is playing, a click only selects the next piece, so that
  // an accidental click does not interrupt the music. The selected piece is
  // played when Play is pressed or when the current piece has finished.
  if (
    index != wxNOT_FOUND && !IsLoadedByMe(m_Files[index])
    && !(player && player->IsPlaying()))
    LoadIndex(index);
  UpdateState();
}

void GOMidiPlaylistDialog::OnListDoubleClick(wxCommandEvent &event) {
  PlayIndex(m_List->GetSelection());
}

void GOMidiPlaylistDialog::OnPrev(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();
  const bool wasPlaying = player && player->IsPlaying();
  const int index = m_List->GetSelection() - 1;

  if (player && wasPlaying)
    player->StopPlaying();
  if (wasPlaying)
    PlayIndex(index);
  else
    LoadIndex(index);
}

void GOMidiPlaylistDialog::OnPlay(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  if (player && player->IsPaused() && IsLoadedByMe(GetSelectedPath()))
    player->Pause(); // continue playing
  else
    PlayIndex(m_List->GetSelection());
}

void GOMidiPlaylistDialog::OnPause(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  if (player)
    player->Pause();
  UpdateState();
}

void GOMidiPlaylistDialog::OnStop(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  if (player)
    player->StopPlaying();
  UpdateState();
}

void GOMidiPlaylistDialog::OnNext(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();
  const bool wasPlaying = player && player->IsPlaying();
  const int index = m_List->GetSelection() + 1;

  if (player && wasPlaying)
    player->StopPlaying();
  if (wasPlaying)
    PlayIndex(index);
  else
    LoadIndex(index);
}

void GOMidiPlaylistDialog::OnSlower(wxCommandEvent &event) {
  ChangeTempo(-(int)GOMidiPlayer::TEMPO_STEP);
}

void GOMidiPlaylistDialog::OnTempoReset(wxCommandEvent &event) {
  GOMidiPlayer *player = r_host.GetMidiPlayer();

  if (player)
    player->SetTempoPercent(100);
  UpdateState();
}

void GOMidiPlaylistDialog::OnFaster(wxCommandEvent &event) {
  ChangeTempo(GOMidiPlayer::TEMPO_STEP);
}

void GOMidiPlaylistDialog::OnAutoAdvance(wxCommandEvent &event) {
  r_config.MidiPlaylistAutoAdvance(m_AutoAdvance->GetValue());
  r_config.Flush();
}

void GOMidiPlaylistDialog::OnRefreshTimer(wxTimerEvent &event) {
  if (IsShown())
    UpdateState();
}

void GOMidiPlaylistDialog::OnClose(wxCloseEvent &event) {
  // only hide the window: the playing goes on and the playlist stays ready
  m_RefreshTimer.Stop();
  Hide();
}

bool GOMidiPlaylistDialog::Show(bool show) {
  if (show) {
    FillList();
    UpdateState();
    m_RefreshTimer.Start(300);
  } else
    m_RefreshTimer.Stop();
  return wxDialog::Show(show);
}
