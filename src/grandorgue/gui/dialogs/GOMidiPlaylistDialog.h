/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIPLAYLISTDIALOG_H
#define GOMIDIPLAYLISTDIALOG_H

#include <functional>
#include <vector>

#include <wx/arrstr.h>
#include <wx/dialog.h>
#include <wx/timer.h>

#include "midi/playlist/GOMidiMarkers.h"

#include "GOTime.h"

class GOConfig;
class GOMidiPlayer;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxListBox;
class wxPanel;
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

    /** @return the current organ gain in dB, used for fading out */
    virtual int GetGain() = 0;

    /**
     * Sets the organ gain without changing the volume setting, used for
     * fading out
     * @param gain the gain in dB
     */
    virtual void SetGain(int gain) = 0;
  };

private:
  GOConfig &r_config;
  Host &r_host;

  wxStaticText *m_FolderText;
  wxChoice *m_ListChoice;
  wxButton *m_EditListButton;
  wxButton *m_DuplicateListButton;
  wxButton *m_DeleteListButton;
  wxListBox *m_List;
  wxButton *m_PrevButton;
  wxButton *m_PlayButton;
  wxButton *m_PauseButton;
  wxButton *m_StopButton;
  wxButton *m_NextButton;
  wxButton *m_FadeOutButton;
  wxSlider *m_Timeline;
  wxPanel *m_MarkersPanel;
  wxButton *m_AddMarkerButton;
  wxButton *m_EditMarkersButton;
  wxStaticText *m_TimeText;
  wxButton *m_SaveTempoButton;
  // whether the user is dragging the timeline thumb now
  bool m_IsSeeking;
  // the last position sent to the player, against double seeking
  int m_LastSeekValue;
  // milliseconds since 1970 do not fit into a 32 bit long (Windows)
  GOTime m_LastSeekMs;
  // the fade out state
  bool m_IsFading;
  bool m_HasFadeStopped; // the piece is stopped, waiting for the release
  int m_FadeStartGain;
  GOTime m_FadeStart;
  wxTimer m_FadeTimer;
  wxStaticText *m_TempoText;
  wxCheckBox *m_AutoAdvance;
  wxCheckBox *m_Repeat;
  wxStaticText *m_StatusText;
  wxTimer m_RefreshTimer;

  wxArrayString m_Files;           // full paths in the order of the list
  std::vector<wxString> m_Moments; // the moment of each piece or empty
  wxArrayString m_ListPaths;       // the prepared lists, the newest first
  wxString m_CurrentListPath;      // the shown prepared list, empty: all pieces
  wxString m_LoadedPath; // the file loaded into the player by this dialog
  // the position of the loaded piece in the list (a piece may be repeated)
  int m_LoadedIndex;
  // the markers of the loaded piece
  GOMidiMarkers m_Markers;
  wxString m_MarkersPath;  // the file the markers belong to
  unsigned m_MarkersTempo; // the tempo the marker buttons show

  void FillList();
  /** @return the MIDI file names of the folder, sorted */
  wxArrayString GetFolderFileNames() const;
  /** Loads the markers of the loaded piece if it has changed */
  void UpdateMarkers();
  void RebuildMarkerButtons();
  /**
   * Opens the list editor
   * @param listPath the list to edit or to copy, empty for a new list
   * @param isCopy whether a copy of listPath is made
   */
  void EditList(const wxString &listPath, bool isCopy);
  wxString GetSelectedPath() const;
  bool IsLoadedByMe(const wxString &path);
  bool LoadIndex(int index);
  void PlayIndex(int index);
  void OnFileFinished();
  void GoToPiece(int index);
  void CancelFadeOut();
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
  void OnFadeOut(wxCommandEvent &event);
  void OnFadeTimer(wxTimerEvent &event);
  void OnNext(wxCommandEvent &event);
  void OnSlower(wxCommandEvent &event);
  void OnTempoReset(wxCommandEvent &event);
  void OnFaster(wxCommandEvent &event);
  void OnAutoAdvance(wxCommandEvent &event);
  void OnRepeat(wxCommandEvent &event);
  void OnListChoice(wxCommandEvent &event);
  void OnNewList(wxCommandEvent &event);
  void OnEditList(wxCommandEvent &event);
  void OnDuplicateList(wxCommandEvent &event);
  void OnDeleteList(wxCommandEvent &event);
  void OnAddMarker(wxCommandEvent &event);
  void OnEditMarkers(wxCommandEvent &event);
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

  // The commands of the buttons, also used by the remote control

  /** Selects a piece: loads it if nothing is playing (a click) */
  void SelectPiece(int index);
  /** Plays a piece from the start (a double click) */
  void PlayPiece(int index);
  /** Plays the selected piece or continues it if paused */
  void PlayOrContinue();
  void TogglePause();
  void Stop();
  /** Goes to the previous piece, playing it if a piece is playing now */
  void Previous();
  /** Goes to the next piece, playing it if a piece is playing now */
  void Next();
  /**
   * Changes the tempo of the player
   * @param delta the change in percent
   */
  void ChangeTempo(int delta);
  void ResetTempo();
  /**
   * Continues the playing from another position of the loaded piece
   * @param positionMs the position from the start of the file
   */
  void SeekTo(double positionMs);
  void SetAutoAdvance(bool isAutoAdvance);
  void SetRepeat(bool isRepeat);
  /**
   * Shows a prepared list
   * @param index the index in GetListNames() or -1 for all pieces
   */
  void SelectList(int index);
  /** Reads the folder again */
  void RefreshList();
  /**
   * Lowers the volume gradually, stops the piece and restores the volume
   * after the sound has died away
   */
  void FadeOut();

  /** @return the full paths of the pieces in the order of the list */
  const wxArrayString &GetFiles() const { return m_Files; }
  int GetSelectedIndex() const;
  /** @return the index of the piece in the player or wxNOT_FOUND */
  int GetLoadedIndex() const;
  bool IsAutoAdvance() const;
  bool IsRepeat() const;
  /** @return the names of the prepared lists, the newest first */
  wxArrayString GetListNames() const;
  /** @return the index of the shown list in GetListNames() or -1 */
  int GetCurrentListIndex() const;
  /** @return the moment of each piece of the list (empty if none) */
  const std::vector<wxString> &GetMoments() const { return m_Moments; }
  /** @return the markers of the loaded piece */
  const GOMidiMarkers &GetMarkers();
  bool IsFading() const { return m_IsFading; }

  DECLARE_EVENT_TABLE()
};

#endif
