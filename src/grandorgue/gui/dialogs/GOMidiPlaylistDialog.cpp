/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiPlaylistDialog.h"

#include <cmath>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/weakref.h>
#include <wx/wrapsizer.h>

#include "config/GOConfig.h"
#include "midi/GOMidiPlayer.h"
#include "midi/files/GOMidiFileTimeScaler.h"
#include "midi/playlist/GOMidiList.h"

#include "GOMidiListEditorDialog.h"
#include "GOMidiMarkersDialog.h"

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
  ID_PLAYLIST_FADE_OUT,
  ID_PLAYLIST_FADE_TIMER,
  ID_PLAYLIST_LIST_CHOICE,
  ID_PLAYLIST_NEW_LIST,
  ID_PLAYLIST_EDIT_LIST,
  ID_PLAYLIST_DUPLICATE_LIST,
  ID_PLAYLIST_DELETE_LIST,
  ID_PLAYLIST_REPEAT,
  ID_PLAYLIST_ADD_MARKER,
  ID_PLAYLIST_EDIT_MARKERS,
};

// the fade out: the volume goes down by FADE_DEPTH_DB in FADE_DURATION_MS,
// then the piece is stopped and the volume is restored after FADE_RELEASE_MS
static const int FADE_STEP_MS = 50;
static const double FADE_DURATION_MS = 5000;
static const double FADE_DEPTH_DB = 40;
static const double FADE_RELEASE_MS = 3000;

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
EVT_BUTTON(ID_PLAYLIST_FADE_OUT, GOMidiPlaylistDialog::OnFadeOut)
EVT_TIMER(ID_PLAYLIST_FADE_TIMER, GOMidiPlaylistDialog::OnFadeTimer)
EVT_CHOICE(ID_PLAYLIST_LIST_CHOICE, GOMidiPlaylistDialog::OnListChoice)
EVT_BUTTON(ID_PLAYLIST_NEW_LIST, GOMidiPlaylistDialog::OnNewList)
EVT_BUTTON(ID_PLAYLIST_EDIT_LIST, GOMidiPlaylistDialog::OnEditList)
EVT_BUTTON(ID_PLAYLIST_DUPLICATE_LIST, GOMidiPlaylistDialog::OnDuplicateList)
EVT_BUTTON(ID_PLAYLIST_DELETE_LIST, GOMidiPlaylistDialog::OnDeleteList)
EVT_CHECKBOX(ID_PLAYLIST_REPEAT, GOMidiPlaylistDialog::OnRepeat)
EVT_BUTTON(ID_PLAYLIST_ADD_MARKER, GOMidiPlaylistDialog::OnAddMarker)
EVT_BUTTON(ID_PLAYLIST_EDIT_MARKERS, GOMidiPlaylistDialog::OnEditMarkers)
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
    m_IsFading(false),
    m_HasFadeStopped(false),
    m_FadeStartGain(0),
    m_FadeStart(0),
    m_FadeTimer(this, ID_PLAYLIST_FADE_TIMER),
    m_RefreshTimer(this, ID_PLAYLIST_TIMER),
    m_LoadedIndex(wxNOT_FOUND),
    m_MarkersTempo(0) {
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

  // the prepared lists
  wxBoxSizer *listSizer = new wxBoxSizer(wxHORIZONTAL);
  listSizer->Add(
    new wxStaticText(this, wxID_ANY, _("List:")),
    0,
    wxALIGN_CENTER_VERTICAL | wxRIGHT,
    5);
  m_ListChoice = new wxChoice(this, ID_PLAYLIST_LIST_CHOICE);
  listSizer->Add(m_ListChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  listSizer->Add(
    new wxButton(this, ID_PLAYLIST_NEW_LIST, _("New list...")), 0, wxRIGHT, 4);
  m_EditListButton = new wxButton(this, ID_PLAYLIST_EDIT_LIST, _("Edit..."));
  listSizer->Add(m_EditListButton, 0, wxRIGHT, 4);
  m_DuplicateListButton
    = new wxButton(this, ID_PLAYLIST_DUPLICATE_LIST, _("Duplicate..."));
  listSizer->Add(m_DuplicateListButton, 0, wxRIGHT, 4);
  m_DeleteListButton = new wxButton(this, ID_PLAYLIST_DELETE_LIST, _("Delete"));
  listSizer->Add(m_DeleteListButton);
  topSizer->Add(listSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

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

  // the markers of the loaded piece
  wxBoxSizer *markersSizer = new wxBoxSizer(wxHORIZONTAL);
  m_MarkersPanel = new wxPanel(this);
  m_MarkersPanel->SetSizer(new wxWrapSizer(wxHORIZONTAL));
  markersSizer->Add(m_MarkersPanel, 1, wxEXPAND | wxRIGHT, 8);
  m_AddMarkerButton
    = new wxButton(this, ID_PLAYLIST_ADD_MARKER, _("Add marker..."));
  markersSizer->Add(m_AddMarkerButton, 0, wxALIGN_TOP | wxRIGHT, 4);
  m_EditMarkersButton
    = new wxButton(this, ID_PLAYLIST_EDIT_MARKERS, _("Markers..."));
  markersSizer->Add(m_EditMarkersButton, 0, wxALIGN_TOP);
  topSizer->Add(markersSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  // the transport buttons
  wxBoxSizer *transportSizer = new wxBoxSizer(wxHORIZONTAL);
  m_PrevButton = new wxButton(this, ID_PLAYLIST_PREV, _("<< Previous"));
  m_PlayButton = new wxButton(this, ID_PLAYLIST_PLAY, _("Play"));
  m_PauseButton = new wxButton(this, ID_PLAYLIST_PAUSE, _("Pause"));
  m_StopButton = new wxButton(this, ID_PLAYLIST_STOP, _("Stop playing"));
  m_NextButton = new wxButton(this, ID_PLAYLIST_NEXT, _("Next >>"));
  m_FadeOutButton = new wxButton(this, ID_PLAYLIST_FADE_OUT, _("Fade out"));
  transportSizer->Add(m_PrevButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_PlayButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_PauseButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_StopButton, 1, wxRIGHT, 4);
  transportSizer->Add(m_FadeOutButton, 1, wxRIGHT, 4);
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
  m_Repeat = new wxCheckBox(
    this, ID_PLAYLIST_REPEAT, _("Start the list again after the last piece"));
  m_Repeat->SetValue(r_config.MidiPlaylistRepeat());
  topSizer->Add(m_Repeat, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

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

GOMidiPlaylistDialog::~GOMidiPlaylistDialog() {
  m_RefreshTimer.Stop();
  m_FadeTimer.Stop();
}

wxArrayString GOMidiPlaylistDialog::GetFolderFileNames() const {
  const wxString folder = r_config.MidiPlaylistPath();
  wxArrayString paths;
  wxArrayString names;

  if (wxDir::Exists(folder)) {
    wxDir::GetAllFiles(folder, &paths, wxT("*.mid"), wxDIR_FILES);
    paths.Sort(compare_file_names);
  }
  for (const wxString &path : paths)
    names.Add(wxFileName(path).GetFullName());
  return names;
}

void GOMidiPlaylistDialog::FillList() {
  const wxString folder = r_config.MidiPlaylistPath();
  const wxString selected = GetSelectedPath();
  const int selectedIndex = m_List->GetSelection();

  m_FolderText->SetLabel(folder);
  m_FolderText->SetToolTip(folder);

  // the prepared lists, the newest first
  m_ListPaths = GOMidiList::getListPaths(folder);
  if (m_ListPaths.Index(m_CurrentListPath) == wxNOT_FOUND)
    m_CurrentListPath = wxEmptyString;
  m_ListChoice->Clear();
  m_ListChoice->Append(_("All pieces of the folder"));
  for (const wxString &listPath : m_ListPaths)
    m_ListChoice->Append(GOMidiList::getListName(listPath));
  m_ListChoice->SetSelection(
    m_CurrentListPath.IsEmpty() ? 0 : m_ListPaths.Index(m_CurrentListPath) + 1);

  // the pieces
  m_Files.Clear();
  m_Moments.clear();
  m_List->Clear();
  if (m_CurrentListPath.IsEmpty()) {
    for (const wxString &fileName : GetFolderFileNames()) {
      m_Files.Add(folder + wxFileName::GetPathSeparator() + fileName);
      m_Moments.push_back(wxEmptyString);
    }
  } else {
    GOMidiList list;

    list.Load(m_CurrentListPath);
    for (const GOMidiList::Item &item : list.GetItems()) {
      m_Files.Add(folder + wxFileName::GetPathSeparator() + item.m_FileName);
      m_Moments.push_back(list.IsMass() ? item.m_moment : wxString());
    }
  }
  for (unsigned n = m_Files.GetCount(), i = 0; i < n; i++) {
    const wxString &path = m_Files[i];
    wxString label = wxFileName(path).GetName();

    if (!m_Moments[i].IsEmpty())
      label = m_Moments[i] + wxT("  -  ") + label;
    if (!wxFileExists(path))
      label += wxT(" ") + _("(not found)");
    m_List->Append(label);
  }

  // the loaded piece may be in the list several times
  if (
    m_LoadedIndex == wxNOT_FOUND || m_LoadedIndex >= (int)m_Files.GetCount()
    || m_Files[m_LoadedIndex] != m_LoadedPath)
    m_LoadedIndex
      = m_LoadedPath.IsEmpty() ? wxNOT_FOUND : m_Files.Index(m_LoadedPath);

  // keep the selection if possible
  int index = selectedIndex >= 0 && selectedIndex < (int)m_Files.GetCount()
      && m_Files[selectedIndex] == selected
    ? selectedIndex
    : m_Files.Index(selected);

  if (index == wxNOT_FOUND)
    index = m_LoadedIndex;
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
    m_LoadedIndex = wxNOT_FOUND;
    isLoaded = r_host.LoadMidiFile(path, [self]() {
      if (self)
        // do not change the player from inside its own timer handler
        self->CallAfter(&GOMidiPlaylistDialog::OnFileFinished);
    });
    if (isLoaded) {
      m_LoadedPath = path;
      m_LoadedIndex = index;
    }
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
  const int current = GetLoadedIndex();
  const int selected = m_List->GetSelection();
  int next = current == wxNOT_FOUND ? wxNOT_FOUND : current + 1;

  // a piece chosen while the current one was playing goes first
  if (selected != wxNOT_FOUND && selected != current)
    next = selected;
  // after the last piece, start the list again if wanted
  if (
    next != wxNOT_FOUND && next >= (int)m_Files.GetCount()
    && m_Repeat->GetValue())
    next = 0;

  if (next != wxNOT_FOUND && next < (int)m_Files.GetCount()) {
    // no automatic start while the music is being faded out
    if (m_AutoAdvance->GetValue() && !m_IsFading)
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
  if (!player || !player->IsLoaded()) {
    m_LoadedPath = wxEmptyString;
    m_LoadedIndex = wxNOT_FOUND;
  }

  m_PrevButton->Enable(player && hasFiles && selected > 0);
  m_NextButton->Enable(
    player && hasFiles && selected != wxNOT_FOUND
    && selected + 1 < (int)m_Files.GetCount());
  m_PlayButton->Enable(player && hasFiles && selected != wxNOT_FOUND);
  m_PauseButton->Enable(player && player->IsPlaying());
  m_PauseButton->SetLabel(
    player && player->IsPaused() ? _("Continue") : _("Pause"));
  m_StopButton->Enable(player && player->IsPlaying());
  m_FadeOutButton->Enable(
    player && player->IsPlaying() && !player->IsPaused() && !m_IsFading);

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

  m_EditListButton->Enable(!m_CurrentListPath.IsEmpty());
  m_DuplicateListButton->Enable(!m_CurrentListPath.IsEmpty());
  m_DeleteListButton->Enable(!m_CurrentListPath.IsEmpty());
  m_AddMarkerButton->Enable(
    player && !m_LoadedPath.IsEmpty() && player->IsPlaying());
  m_EditMarkersButton->Enable(
    !m_LoadedPath.IsEmpty() && m_Markers.GetCount() > 0);
  UpdateMarkers();
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
    SeekTo(lengthMs * value / TIMELINE_STEPS);
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
        const int loadedIndex = GetLoadedIndex();
        GOMidiMarkers markers;

        // the markers move with the music: keep the original ones too
        markers.Load(path);
        if (markers.GetCount() > 0) {
          const wxString markersPath = GOMidiMarkers::getMarkersPath(path);
          const wxString markersBackup = backupDir
            + wxFileName::GetPathSeparator()
            + wxFileName(markersPath).GetFullName();

          if (!wxFileExists(markersBackup))
            wxCopyFile(markersPath, markersBackup, false);
          markers.Scale(100.0 / tempo);
          markers.Save(path);
        }
        m_MarkersPath = wxEmptyString; // read them again
        // the file now has the new tempo: play it at 100% from now on
        pPlayer->SetTempoPercent(100);
        LoadIndex(loadedIndex);
      }
    }
  }
  UpdateState();
}

void GOMidiPlaylistDialog::ForgetLoadedFile() {
  m_LoadedPath = wxEmptyString;
  m_LoadedIndex = wxNOT_FOUND;
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
  RefreshList();
}

void GOMidiPlaylistDialog::SelectPiece(int index) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const bool isValid = index >= 0 && index < (int)m_Files.GetCount();

  if (isValid)
    m_List->SetSelection(index);
  // While a piece is playing, a click only selects the next piece, so that
  // an accidental click does not interrupt the music. The selected piece is
  // played when Play is pressed or when the current piece has finished.
  if (
    isValid && !IsLoadedByMe(m_Files[index])
    && !(pPlayer && pPlayer->IsPlaying()))
    LoadIndex(index);
  UpdateState();
}

void GOMidiPlaylistDialog::PlayPiece(int index) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  CancelFadeOut();
  // a double click on the playing piece starts it again
  if (pPlayer && pPlayer->IsPlaying())
    pPlayer->StopPlaying();
  PlayIndex(index);
}

void GOMidiPlaylistDialog::PlayOrContinue() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  CancelFadeOut();
  if (pPlayer && pPlayer->IsPaused() && IsLoadedByMe(GetSelectedPath()))
    pPlayer->Pause(); // continue playing
  else if (!(pPlayer && pPlayer->IsPlaying()
             && IsLoadedByMe(GetSelectedPath())))
    PlayIndex(m_List->GetSelection());
  UpdateState();
}

void GOMidiPlaylistDialog::TogglePause() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer)
    pPlayer->Pause();
  UpdateState();
}

void GOMidiPlaylistDialog::Stop() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer)
    pPlayer->StopPlaying();
  UpdateState();
}

void GOMidiPlaylistDialog::GoToPiece(int index) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const bool wasPlaying = pPlayer && pPlayer->IsPlaying();

  if (index >= 0 && index < (int)m_Files.GetCount()) {
    CancelFadeOut();
    if (wasPlaying) {
      pPlayer->StopPlaying();
      PlayIndex(index);
    } else
      LoadIndex(index);
  }
  UpdateState();
}

void GOMidiPlaylistDialog::Previous() { GoToPiece(m_List->GetSelection() - 1); }

void GOMidiPlaylistDialog::Next() { GoToPiece(m_List->GetSelection() + 1); }

void GOMidiPlaylistDialog::ResetTempo() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer)
    pPlayer->SetTempoPercent(100);
  UpdateState();
}

void GOMidiPlaylistDialog::SeekTo(double positionMs) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer && !m_LoadedPath.IsEmpty()) {
    CancelFadeOut();
    pPlayer->SeekTo(positionMs);
  }
  UpdateState();
}

void GOMidiPlaylistDialog::SetAutoAdvance(bool isAutoAdvance) {
  m_AutoAdvance->SetValue(isAutoAdvance);
  r_config.MidiPlaylistAutoAdvance(isAutoAdvance);
  r_config.Flush();
}

void GOMidiPlaylistDialog::RefreshList() {
  FillList();
  UpdateState();
}

int GOMidiPlaylistDialog::GetSelectedIndex() const {
  return m_List->GetSelection();
}

int GOMidiPlaylistDialog::GetLoadedIndex() const {
  int index = wxNOT_FOUND;

  if (!m_LoadedPath.IsEmpty())
    index = m_LoadedIndex >= 0 && m_LoadedIndex < (int)m_Files.GetCount()
        && m_Files[m_LoadedIndex] == m_LoadedPath
      ? m_LoadedIndex
      : m_Files.Index(m_LoadedPath);
  return index;
}

bool GOMidiPlaylistDialog::IsRepeat() const { return m_Repeat->GetValue(); }

void GOMidiPlaylistDialog::SetRepeat(bool isRepeat) {
  m_Repeat->SetValue(isRepeat);
  r_config.MidiPlaylistRepeat(isRepeat);
  r_config.Flush();
}

wxArrayString GOMidiPlaylistDialog::GetListNames() const {
  wxArrayString names;

  for (const wxString &listPath : m_ListPaths)
    names.Add(GOMidiList::getListName(listPath));
  return names;
}

int GOMidiPlaylistDialog::GetCurrentListIndex() const {
  return m_CurrentListPath.IsEmpty() ? -1
                                     : m_ListPaths.Index(m_CurrentListPath);
}

void GOMidiPlaylistDialog::SelectList(int index) {
  m_CurrentListPath = index >= 0 && index < (int)m_ListPaths.GetCount()
    ? m_ListPaths[index]
    : wxString();
  FillList();
  UpdateState();
}

const GOMidiMarkers &GOMidiPlaylistDialog::GetMarkers() {
  UpdateMarkers();
  return m_Markers;
}

void GOMidiPlaylistDialog::UpdateMarkers() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const unsigned tempo = pPlayer ? pPlayer->GetTempoPercent() : 100;

  if (m_LoadedPath != m_MarkersPath) {
    m_MarkersPath = m_LoadedPath;
    if (m_LoadedPath.IsEmpty())
      m_Markers.Clear();
    else
      m_Markers.Load(m_LoadedPath);
    m_MarkersTempo = 0; // rebuild the buttons
  }
  if (m_MarkersTempo != tempo) {
    m_MarkersTempo = tempo;
    RebuildMarkerButtons();
  }
}

void GOMidiPlaylistDialog::RebuildMarkerButtons() {
  m_MarkersPanel->DestroyChildren();
  for (const GOMidiMarkers::Marker &marker : m_Markers.GetMarkers()) {
    const double position = marker.m_position;
    wxButton *pButton = new wxButton(
      m_MarkersPanel,
      wxID_ANY,
      marker.m_name + wxT("  ") + FormatTime(position),
      wxDefaultPosition,
      wxDefaultSize,
      wxBU_EXACTFIT);

    pButton->Bind(
      wxEVT_BUTTON, [this, position](wxCommandEvent &) { SeekTo(position); });
    m_MarkersPanel->GetSizer()->Add(pButton, 0, wxRIGHT | wxBOTTOM, 4);
  }
  m_MarkersPanel->Layout();
  Layout();
}

void GOMidiPlaylistDialog::EditList(const wxString &listPath, bool isCopy) {
  const wxString folder = r_config.MidiPlaylistPath();
  GOMidiList list;
  wxString name;
  wxString title;

  if (listPath.IsEmpty()) {
    list.SetMass(true);
    name = _("Mass") + wxT(" ") + wxDateTime::Now().Format(wxT("%d-%m-%Y"));
    title = _("New list");
  } else {
    list.Load(listPath);
    name = isCopy
      ? (list.IsMass() ? _("Mass") : GOMidiList::getListName(listPath))
        + wxT(" ") + wxDateTime::Now().Format(wxT("%d-%m-%Y"))
      : GOMidiList::getListName(listPath);
    title = isCopy ? _("Duplicate list") : _("Edit list");
  }

  GOMidiListEditorDialog dlg(this, title, name, list, GetFolderFileNames());
  bool isDone = false;

  while (!isDone && dlg.ShowModal() == wxID_OK) {
    const wxString newPath = GOMidiList::getListPath(folder, dlg.GetName());
    const bool isSameFile = !isCopy && newPath == listPath;

    if (
      !isSameFile && wxFileExists(newPath)
      && wxMessageBox(
           wxString::Format(
             _("A list called \"%s\" already exists. Replace it?"),
             dlg.GetName()),
           title,
           wxYES_NO | wxICON_QUESTION,
           this)
        != wxYES) {
      // ask for another name
    } else if (!dlg.GetList().Save(newPath))
      wxMessageBox(
        _("Could not save the list."), title, wxOK | wxICON_ERROR, this);
    else {
      // a renamed list: remove the old file
      if (!listPath.IsEmpty() && !isCopy && !isSameFile)
        wxRemoveFile(listPath);
      m_CurrentListPath = newPath;
      isDone = true;
    }
  }
  FillList();
  UpdateState();
}

void GOMidiPlaylistDialog::OnListChoice(wxCommandEvent &event) {
  SelectList(m_ListChoice->GetSelection() - 1);
}

void GOMidiPlaylistDialog::OnNewList(wxCommandEvent &event) {
  EditList(wxEmptyString, false);
}

void GOMidiPlaylistDialog::OnEditList(wxCommandEvent &event) {
  if (!m_CurrentListPath.IsEmpty())
    EditList(m_CurrentListPath, false);
}

void GOMidiPlaylistDialog::OnDuplicateList(wxCommandEvent &event) {
  if (!m_CurrentListPath.IsEmpty())
    EditList(m_CurrentListPath, true);
}

void GOMidiPlaylistDialog::OnDeleteList(wxCommandEvent &event) {
  if (
    !m_CurrentListPath.IsEmpty()
    && wxMessageBox(
         wxString::Format(
           _("Delete the list \"%s\"? The pieces themselves are not "
             "deleted."),
           GOMidiList::getListName(m_CurrentListPath)),
         _("Delete list"),
         wxYES_NO | wxICON_QUESTION,
         this)
      == wxYES) {
    wxRemoveFile(m_CurrentListPath);
    m_CurrentListPath = wxEmptyString;
    FillList();
  }
  UpdateState();
}

void GOMidiPlaylistDialog::OnRepeat(wxCommandEvent &event) {
  SetRepeat(m_Repeat->GetValue());
}

void GOMidiPlaylistDialog::OnAddMarker(wxCommandEvent &event) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (pPlayer && !m_LoadedPath.IsEmpty() && pPlayer->IsPlaying()) {
    const double position = pPlayer->GetPosition();
    wxTextEntryDialog dlg(
      this,
      wxString::Format(_("Name of the marker at %s:"), FormatTime(position)),
      _("Add marker"),
      wxString::Format(_("Marker %u"), m_Markers.GetCount() + 1));

    if (dlg.ShowModal() == wxID_OK && !dlg.GetValue().Trim().IsEmpty()) {
      UpdateMarkers();
      m_Markers.Add(position, dlg.GetValue().Trim());
      if (!m_Markers.Save(m_LoadedPath))
        wxMessageBox(
          _("Could not save the markers."),
          _("Add marker"),
          wxOK | wxICON_ERROR,
          this);
      RebuildMarkerButtons();
    }
  }
  UpdateState();
}

void GOMidiPlaylistDialog::OnEditMarkers(wxCommandEvent &event) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (!m_LoadedPath.IsEmpty()) {
    const wxString path = m_LoadedPath;

    UpdateMarkers();

    GOMidiMarkersDialog dlg(
      this,
      m_Markers,
      pPlayer ? pPlayer->GetTempoPercent() : 100,
      [this](double position) { SeekTo(position); });

    dlg.ShowModal();
    if (!m_Markers.Save(path))
      wxMessageBox(
        _("Could not save the markers."),
        _("Markers"),
        wxOK | wxICON_ERROR,
        this);
    RebuildMarkerButtons();
  }
  UpdateState();
}

bool GOMidiPlaylistDialog::IsAutoAdvance() const {
  return m_AutoAdvance->GetValue();
}

void GOMidiPlaylistDialog::FadeOut() {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();

  if (!m_IsFading && pPlayer && pPlayer->IsPlaying() && !pPlayer->IsPaused()) {
    m_IsFading = true;
    m_HasFadeStopped = false;
    m_FadeStartGain = r_host.GetGain();
    m_FadeStart = wxGetLocalTimeMillis();
    m_FadeTimer.Start(FADE_STEP_MS);
  }
  UpdateState();
}

void GOMidiPlaylistDialog::CancelFadeOut() {
  if (m_IsFading) {
    m_FadeTimer.Stop();
    r_host.SetGain(m_FadeStartGain);
    m_IsFading = false;
  }
}

void GOMidiPlaylistDialog::OnFadeTimer(wxTimerEvent &event) {
  GOMidiPlayer *pPlayer = r_host.GetMidiPlayer();
  const double elapsedMs = (wxGetLocalTimeMillis() - m_FadeStart).ToDouble();

  if (!m_HasFadeStopped) {
    if (!pPlayer || !pPlayer->IsPlaying() || elapsedMs >= FADE_DURATION_MS) {
      // the volume is low now: stop the piece and let the release of the
      // pipes and the reverb die away before restoring the volume
      if (pPlayer && pPlayer->IsPlaying())
        pPlayer->StopPlaying();
      m_HasFadeStopped = true;
      m_FadeStart = wxGetLocalTimeMillis();
    } else
      r_host.SetGain(
        m_FadeStartGain
        - (int)lround(FADE_DEPTH_DB * elapsedMs / FADE_DURATION_MS));
  } else if (elapsedMs >= FADE_RELEASE_MS) {
    m_FadeTimer.Stop();
    r_host.SetGain(m_FadeStartGain);
    m_IsFading = false;
  }
  UpdateState();
}

void GOMidiPlaylistDialog::OnListSelect(wxCommandEvent &event) {
  SelectPiece(m_List->GetSelection());
}

void GOMidiPlaylistDialog::OnListDoubleClick(wxCommandEvent &event) {
  PlayPiece(m_List->GetSelection());
}

void GOMidiPlaylistDialog::OnPrev(wxCommandEvent &event) { Previous(); }

void GOMidiPlaylistDialog::OnPlay(wxCommandEvent &event) { PlayOrContinue(); }

void GOMidiPlaylistDialog::OnPause(wxCommandEvent &event) { TogglePause(); }

void GOMidiPlaylistDialog::OnStop(wxCommandEvent &event) { Stop(); }

void GOMidiPlaylistDialog::OnFadeOut(wxCommandEvent &event) { FadeOut(); }

void GOMidiPlaylistDialog::OnNext(wxCommandEvent &event) { Next(); }

void GOMidiPlaylistDialog::OnSlower(wxCommandEvent &event) {
  ChangeTempo(-(int)GOMidiPlayer::TEMPO_STEP);
}

void GOMidiPlaylistDialog::OnTempoReset(wxCommandEvent &event) { ResetTempo(); }

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
