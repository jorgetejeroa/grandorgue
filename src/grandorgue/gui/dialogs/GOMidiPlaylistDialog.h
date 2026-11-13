/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIPLAYLISTDIALOG_H
#define GOMIDIPLAYLISTDIALOG_H

#include <functional>

#include <wx/arrstr.h>
#include <wx/dialog.h>
#include <wx/timer.h>

#include "GOTime.h"

class GOConfig;
class GOMidiPlayer;
class wxButton;
class wxCheckBox;
class wxListBox;
class wxScrollEvent;
class wxSlider;
class wxStaticText;

/**
 * A modeless window that shows all MIDI files of a folder as a playlist.
 * One click on a file loads it into the MIDI player, a double click loads
 * and plays it. Optionally the next file is played automatically when the
 * current one has finished. It also allows changing the playing tempo.
 */
class GOMidiPlaylistDialog : public wxDialog {
public:
  /**
   * The services the dialog needs from the main window
   */
  class Host {
  public:
    virtual ~Host() {}

    /**
     * @return the MIDI player of the loaded organ or nullptr if no organ is
     *   loaded
     */
    virtual GOMidiPlayer *GetMidiPlayer() = 0;

    /**
     * Loads a MIDI file into the MIDI player of the current organ
     * @param path the file to load
     * @param onFinished called when the file has been played to its end
     * @return true if the file has been loaded
     */
    virtual bool LoadMidiFile(
      const wxString &path, std::function<void()> onFinished)
      = 0;
  };

private:
  GOConfig &r_config;
  Host &r_host;

  wxStaticText *m_FolderText;
  wxListBox *m_List;
  wxButton *m_PrevButton;
  wxButton *m_PlayButton;
  wxButton *m_PauseButton;
  wxButton *m_StopButton;
  wxButton *m_NextButton;
  wxSlider *m_Timeline;
  wxStaticText *m_TimeText;
  wxButton *m_SaveTempoButton;
  // whether the user is dragging the timeline thumb now
  bool m_IsSeeking;
  // the last position sent to the player, against double seeking
  int m_LastSeekValue;
  // milliseconds since 1970 do not fit into a 32 bit long (Windows)
  GOTime m_LastSeekMs;
  wxStaticText *m_TempoText;
  wxCheckBox *m_AutoAdvance;
  wxStaticText *m_StatusText;
  wxTimer m_RefreshTimer;

  wxArrayString m_Files; // full paths in the order of the list
  wxString m_LoadedPath; // the file loaded into the player by this dialog

  void FillList();
  wxString GetSelectedPath() const;
  bool IsLoadedByMe(const wxString &path);
  bool LoadIndex(int index);
  void PlayIndex(int index);
  void OnFileFinished();
  void ChangeTempo(int delta);
  void UpdateState();
  void UpdateTimeline();
  void SeekToTimeline();
  /** @return the real duration at the current tempo as m:ss */
  wxString FormatTime(double fileTimeMs) const;

  void OnChooseFolder(wxCommandEvent &event);
  void OnRefreshList(wxCommandEvent &event);
  void OnListSelect(wxCommandEvent &event);
  void OnListDoubleClick(wxCommandEvent &event);
  void OnPrev(wxCommandEvent &event);
  void OnPlay(wxCommandEvent &event);
  void OnPause(wxCommandEvent &event);
  void OnStop(wxCommandEvent &event);
  void OnNext(wxCommandEvent &event);
  void OnSlower(wxCommandEvent &event);
  void OnTempoReset(wxCommandEvent &event);
  void OnFaster(wxCommandEvent &event);
  void OnAutoAdvance(wxCommandEvent &event);
  void OnSaveTempo(wxCommandEvent &event);
  void OnTimelineTrack(wxScrollEvent &event);
  void OnTimelineRelease(wxScrollEvent &event);
  void OnRefreshTimer(wxTimerEvent &event);
  void OnClose(wxCloseEvent &event);

public:
  GOMidiPlaylistDialog(wxWindow *parent, GOConfig &config, Host &host);
  ~GOMidiPlaylistDialog();

  bool Show(bool show = true) override;

  /**
   * Called when a MIDI file has been loaded outside the playlist, so the
   * playlist no longer owns the player content
   */
  void ForgetLoadedFile();

  DECLARE_EVENT_TABLE()
};

#endif
