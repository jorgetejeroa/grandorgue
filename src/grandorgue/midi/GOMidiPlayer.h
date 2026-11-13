/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIPLAYER_H
#define GOMIDIPLAYER_H

#include <wx/string.h>
#include <wx/timer.h>

#include <functional>
#include <optional>
#include <vector>

#include "config/GOConfig.h"
#include "control/GOElementCreator.h"
#include "control/GOLabelControl.h"
#include "midi/GOMidiPlayerContent.h"

#include "GOTime.h"
#include "GOTimerCallback.h"

class GOMidiSystem;
class GOMidiMap;
class GOMidiEvent;
class GOMidiFileReader;
class GOOrganController;
class GOTimer;

class GOMidiPlayer : public GOElementCreator, private GOTimerCallback {
private:
  GOConfig &r_Config;
  GOMidiMap &r_MidiMap;
  GOTimer &r_timer;
  GOMidiSystem *p_midi;
  GOMidiPlayerContent m_content;
  GOLabelControl m_PlayingTime;
  GOLabelControl m_TempoLabel;
  GOTime m_Start;
  unsigned m_PlayingSeconds;
  // the time scale factor: 1 means the recorded tempo, 2 means half speed
  float m_Speed;
  // the playing tempo in percent of the recorded tempo
  unsigned m_TempoPercent;
  bool m_IsPlaying;
  bool m_Pause;
  unsigned m_DeviceID;
  // called when a file has been played to its end (not on manual stop)
  std::function<void()> m_OnFinished;

  void ButtonStateChanged(int id, bool newState) override;

  /**
   * Decides which MIDI channel mapping scheme to use for a MIDI file, asking
   * chooseMapping if the file lacks a native header and asking is enabled in
   * settings.
   * @param events the MIDI events read from the file, in time order
   * @param midiInputNumbers see GOMidiPlayerContent::computeManualChannels()
   * @param chooseMapping see LoadFile()
   * @return the mapping to use, or std::nullopt if the user cancelled
   */
  std::optional<GOConfig::MidiFileChannelMapping> DetermineMappingMode(
    const std::vector<GOMidiEvent> &events,
    const std::vector<int> &midiInputNumbers,
    const GOConfig::MidiChannelMappingChooser &chooseMapping);

  void UpdateDisplay();
  void UpdateTempoDisplay();

  /**
   * Set the buttons and the display to the initial state
   */
  void ResetUI();
  /**
   * Send midi event to the organ
   * @param event the event to process
   */
  void PlayMidiEvent(const GOMidiEvent &e);
  /**
   * Converts a time in the MIDI file to the real time with the current tempo
   * @param fileTimeMs the time from the start of the file in milliseconds
   * @return the real time when it should be played
   */
  GOTime ToRealTime(double fileTimeMs) const;
  void HandleTimer() override;

public:
  GOMidiPlayer(GOOrganController *organController);
  ~GOMidiPlayer();

  /**
   * Set up for playing any midi
   * @param pMidi - a pointer to the midi engine
   */
  void Setup(GOMidiSystem *pMidi) { p_midi = pMidi; }

  /**
   * Loads a MIDI file for playback.
   * @param filename the MIDI file to load
   * @param hasPedal see GOMidiPlayerContent::computeManualChannels()
   * @param midiInputNumbers see GOMidiPlayerContent::computeManualChannels()
   * @param chooseMapping asks which channel mapping scheme to use if
   *   filename lacks GrandOrgue's own setup header; not called if asking is
   *   disabled in settings
   */
  void LoadFile(
    const wxString &filename,
    bool hasPedal,
    const std::vector<int> &midiInputNumbers,
    const GOConfig::MidiChannelMappingChooser &chooseMapping);
  bool IsLoaded();

  void Play();
  void Pause();
  void StopPlaying();
  bool IsPlaying();
  bool IsPaused() const { return m_IsPlaying && m_Pause; }

  static constexpr unsigned TEMPO_MIN = 25;
  static constexpr unsigned TEMPO_MAX = 200;
  static constexpr unsigned TEMPO_STEP = 5;

  unsigned GetTempoPercent() const { return m_TempoPercent; }
  /**
   * Changes the playing tempo. May be called while playing: the playback
   * continues from the current position with the new tempo.
   * @param percent the tempo in percent of the recorded tempo
   */
  void SetTempoPercent(unsigned percent);

  /** @return the length of the loaded file in milliseconds (file time) */
  GOTime GetLength() const { return m_content.GetLength(); }

  /**
   * @return the current playing position in milliseconds from the start of
   *   the file (file time, independent of the tempo); 0 if not playing
   */
  double GetPosition() const;

  /**
   * Continues the playing from another position of the loaded file. All
   * events before the position except the notes are sent at once, so the
   * registration is the same as at that point of the recording, and the
   * notes held at that point are started again. Starts playing if the
   * player is stopped or paused.
   * @param positionMs the position in milliseconds from the start of the file
   */
  void SeekTo(double positionMs);

  /**
   * Sets a function to be called when a file has been played to its end.
   * It is not called when the playing is stopped by the user.
   */
  void SetOnFinished(std::function<void()> onFinished) {
    m_OnFinished = onFinished;
  }

  void Load(GOConfigReader &cfg) override;
  GOEnclosure *GetEnclosure(const wxString &name, bool is_panel) override;
  GOLabelControl *GetLabelControl(const wxString &name, bool is_panel) override;

  /**
   * Clean up. Playing will be impossible until Setup is called
   */
  void Cleanup();
};

#endif
