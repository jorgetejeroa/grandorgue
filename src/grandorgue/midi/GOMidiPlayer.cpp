/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiPlayer.h"

#include <wx/intl.h>

#include "config/GOConfig.h"
#include "control/GOCallbackButtonControl.h"
#include "midi/events/GOMidiEvent.h"
#include "midi/files/GOMidiFileReader.h"
#include "midi/objects/GOMidiObjectContext.h"

#include "GOEvent.h"
#include "GOMidiMap.h"
#include "GOMidiSystem.h"
#include "GOOrganController.h"
#include "GOTimer.h"

enum {
  ID_MIDI_PLAYER_PLAY = 0,
  ID_MIDI_PLAYER_STOP,
  ID_MIDI_PLAYER_PAUSE,
  ID_MIDI_PLAYER_SLOWER,
  ID_MIDI_PLAYER_TEMPO_RESET,
  ID_MIDI_PLAYER_FASTER,
};

static const GOMidiObjectContext MIDI_CONTEXT(
  wxT("MidiPlayer"), _("MidiPlayer"));

const GOElementCreator::ButtonDefinitionEntry BUTTON_DEFS[] = {
  {wxT("MidiPlayerPlay"),
   ID_MIDI_PLAYER_PLAY,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT("MidiPlayerStop"),
   ID_MIDI_PLAYER_STOP,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT("MidiPlayerPause"),
   ID_MIDI_PLAYER_PAUSE,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT("MidiPlayerSlower"),
   ID_MIDI_PLAYER_SLOWER,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT("MidiPlayerTempoReset"),
   ID_MIDI_PLAYER_TEMPO_RESET,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT("MidiPlayerFaster"),
   ID_MIDI_PLAYER_FASTER,
   false,
   true,
   false,
   &MIDI_CONTEXT},
  {wxT(""), -1, false, false, false},
};

void GOMidiPlayer::ResetUI() {
  m_buttons[ID_MIDI_PLAYER_PLAY]->Display(false);
  m_buttons[ID_MIDI_PLAYER_PAUSE]->Display(false);
  UpdateDisplay();
}

GOMidiPlayer::GOMidiPlayer(GOOrganController *organController)
  : r_Config(organController->GetSettings()),
    r_MidiMap(organController->GetSettings().GetMidiMap()),
    r_timer(*organController->GetTimer()),
    p_midi(nullptr),
    m_content(),
    m_PlayingTime(*organController, &MIDI_CONTEXT),
    m_TempoLabel(*organController, &MIDI_CONTEXT),
    m_Start(0),
    m_PlayingSeconds(0),
    m_Speed(1),
    m_TempoPercent(100),
    m_IsPlaying(false),
    m_Pause(false) {
  CreateButtons(*organController, BUTTON_DEFS);
  m_DeviceID = r_MidiMap.EnsureLogicalName(_("GrandOrgue MIDI Player"));
  ResetUI();
  UpdateTempoDisplay();
}

GOMidiPlayer::~GOMidiPlayer() { Cleanup(); }

void GOMidiPlayer::Load(GOConfigReader &cfg) {
  m_buttons[ID_MIDI_PLAYER_PLAY]->Init(cfg, wxT("MidiPlayerPlay"), _("PLAY"));
  m_buttons[ID_MIDI_PLAYER_STOP]->Init(cfg, wxT("MidiPlayerStop"), _("STOP"));
  m_buttons[ID_MIDI_PLAYER_PAUSE]->Init(
    cfg, wxT("MidiPlayerPause"), _("PAUSE"));
  m_PlayingTime.Init(cfg, wxT("MidiPlayerTime"), _("MIDI playing time"));
  m_buttons[ID_MIDI_PLAYER_SLOWER]->Init(
    cfg, wxT("MidiPlayerSlower"), _("SLOWER"));
  m_buttons[ID_MIDI_PLAYER_TEMPO_RESET]->Init(
    cfg, wxT("MidiPlayerTempoReset"), _("TEMPO 100%"));
  m_buttons[ID_MIDI_PLAYER_FASTER]->Init(
    cfg, wxT("MidiPlayerFaster"), _("FASTER"));
  m_TempoLabel.Init(cfg, wxT("MidiPlayerTempo"), _("MIDI player tempo"));
  UpdateTempoDisplay();
}

void GOMidiPlayer::ButtonStateChanged(int id, bool newState) {
  switch (id) {
  case ID_MIDI_PLAYER_STOP:
    StopPlaying();
    break;

  case ID_MIDI_PLAYER_PLAY:
    Play();
    break;

  case ID_MIDI_PLAYER_PAUSE:
    Pause();
    break;

  case ID_MIDI_PLAYER_SLOWER:
    if (m_TempoPercent > TEMPO_MIN)
      SetTempoPercent(m_TempoPercent - TEMPO_STEP);
    break;

  case ID_MIDI_PLAYER_TEMPO_RESET:
    SetTempoPercent(100);
    break;

  case ID_MIDI_PLAYER_FASTER:
    SetTempoPercent(m_TempoPercent + TEMPO_STEP);
    break;
  }
}

std::optional<GOConfig::MidiFileChannelMapping> GOMidiPlayer::
  DetermineMappingMode(
    const std::vector<GOMidiEvent> &events,
    const std::vector<int> &midiInputNumbers,
    const GOConfig::MidiChannelMappingChooser &chooseMapping) {
  std::optional<GOConfig::MidiFileChannelMapping> mappingMode
    = GOConfig::MIDI_PLAY_CHANNELS_PEDAL_LAST;

  if (!events.empty() && !GOMidiPlayerContent::hasNativeHeader(events)) {
    const bool isInputNumberUsable
      = GOMidiPlayerContent::isMidiInputNumberMappingUsable(midiInputNumbers);
    const GOConfig::MidiFileChannelMapping defaultMapping = isInputNumberUsable
      ? r_Config.MidiPlayerChannelMappingWithInputNumber()
      : r_Config.MidiPlayerChannelMappingWithoutInputNumber();

    mappingMode = r_Config.IsToAskMidiPlayerChannelMapping()
      ? chooseMapping(isInputNumberUsable, defaultMapping)
      : std::optional(defaultMapping);
  }

  return mappingMode;
}

void GOMidiPlayer::LoadFile(
  const wxString &filename,
  bool hasPedal,
  const std::vector<int> &midiInputNumbers,
  const GOConfig::MidiChannelMappingChooser &chooseMapping) {
  StopPlaying();
  m_content.Clear();

  GOMidiFileReader reader(r_MidiMap);

  if (!reader.Open(filename))
    GOMessageBox(
      wxString::Format(_("Failed to load %s"), filename.c_str()),
      _("MIDI Player"),
      wxOK | wxICON_ERROR,
      NULL);
  else {
    std::vector<GOMidiEvent> events;

    reader.ReadAllEvents(events);

    const std::optional<GOConfig::MidiFileChannelMapping> mappingMode
      = DetermineMappingMode(events, midiInputNumbers, chooseMapping);

    if (!mappingMode) {
      // the user cancelled the channel mapping dialog: silently abort
    } else if (!m_content.Load(
                 events, r_MidiMap, hasPedal, midiInputNumbers, *mappingMode)) {
      m_content.Clear();
      GOMessageBox(
        wxString::Format(_("Failed to load %s"), filename.c_str()),
        _("MIDI Player"),
        wxOK | wxICON_ERROR,
        NULL);
    } else if (!reader.Close())
      GOMessageBox(
        wxString::Format(_("Failed to decode %s"), filename.c_str()),
        _("MIDI Player"),
        wxOK | wxICON_ERROR,
        NULL);
  }
}

bool GOMidiPlayer::IsLoaded() { return m_content.IsLoaded(); }

void GOMidiPlayer::Play() {
  StopPlaying();
  m_content.Reset();
  m_Start = wxGetLocalTimeMillis();
  m_PlayingSeconds = 0;
  m_IsPlaying = IsLoaded();
  m_Pause = false;
  if (m_IsPlaying) {
    m_buttons[ID_MIDI_PLAYER_PLAY]->Display(true);
    UpdateDisplay();
    HandleTimer();
  } else
    StopPlaying();
}

void GOMidiPlayer::Pause() {
  if (!m_IsPlaying)
    return;
  if (m_Pause) {
    m_Pause = false;
    m_buttons[ID_MIDI_PLAYER_PAUSE]->Display(m_Pause);
    m_Start = wxGetLocalTimeMillis() - m_Start;
    HandleTimer();
  } else {
    m_Pause = true;
    m_buttons[ID_MIDI_PLAYER_PAUSE]->Display(m_Pause);
    m_Start = wxGetLocalTimeMillis() - m_Start;
    r_timer.DeleteTimer(this);
  }
}

void GOMidiPlayer::PlayMidiEvent(const GOMidiEvent &e) {
  GOMidiSystem *pMidi = p_midi;

  if (pMidi)
    pMidi->Recv(e);
}

void GOMidiPlayer::StopPlaying() {
  if (m_IsPlaying) {
    for (unsigned i = 1; i < 16; i++) {
      GOMidiEvent e;
      e.SetMidiType(GOMidiEvent::MIDI_CTRL_CHANGE);
      e.SetChannel(i);
      e.SetKey(MIDI_CTRL_NOTES_OFF);
      e.SetValue(0);
      e.SetDevice(m_DeviceID);
      e.SetTime(wxGetLocalTimeMillis());
      e.SetAllowedToReload(false);
      PlayMidiEvent(e);
    }
  }

  m_IsPlaying = false;
  ResetUI();
  r_timer.DeleteTimer(this);
}

bool GOMidiPlayer::IsPlaying() { return m_IsPlaying; }

void GOMidiPlayer::UpdateDisplay() {
  if (!IsLoaded())
    m_PlayingTime.SetContent(_("<no file loaded>"));
  else if (!IsPlaying())
    m_PlayingTime.SetContent(_("-:--:--"));
  else
    m_PlayingTime.SetContent(wxString::Format(
      _("%d:%02d:%02d"),
      m_PlayingSeconds / 3600,
      (m_PlayingSeconds / 60) % 60,
      m_PlayingSeconds % 60));
}

GOTime GOMidiPlayer::ToRealTime(double fileTimeMs) const {
  // wxLongLong * float would truncate the factor to an integer, so the
  // scaling must be done in floating point
  return m_Start + (wxLongLong_t)(fileTimeMs * m_Speed);
}

void GOMidiPlayer::HandleTimer() {
  if (!m_IsPlaying)
    return;
  GOTime now = wxGetLocalTimeMillis();
  if (ToRealTime((m_PlayingSeconds + 1) * 1000.0) <= now) {
    m_PlayingSeconds++;
    UpdateDisplay();
  }
  do {
    GOMidiEvent e = m_content.GetCurrentEvent();
    const GOTime eventTime = ToRealTime(e.GetTime().ToDouble());

    if (eventTime <= now) {
      if (!m_content.Next()) {
        StopPlaying();
        if (m_OnFinished)
          m_OnFinished();
        return;
      }
      e.SetDevice(m_DeviceID);
      e.SetTime(wxGetLocalTimeMillis());
      e.SetAllowedToReload(false);
      PlayMidiEvent(e);
    } else {
      const GOTime nextSecond = ToRealTime((m_PlayingSeconds + 1) * 1000.0);

      r_timer.SetTimer(eventTime < nextSecond ? eventTime : nextSecond, this);
      return;
    }
  } while (true);
}

void GOMidiPlayer::UpdateTempoDisplay() {
  m_TempoLabel.SetContent(wxString::Format(_("Tempo %u%%"), m_TempoPercent));
}

void GOMidiPlayer::SetTempoPercent(unsigned percent) {
  if (percent < TEMPO_MIN)
    percent = TEMPO_MIN;
  if (percent > TEMPO_MAX)
    percent = TEMPO_MAX;
  if (percent != m_TempoPercent) {
    const float newSpeed = 100.0f / percent;

    if (m_IsPlaying) {
      // Keep the current position in the file and continue with the new
      // tempo from there. While paused, m_Start holds the elapsed real time
      // instead of the start time (see Pause())
      if (m_Pause) {
        const double elapsed = m_Start.ToDouble();

        m_Start = (wxLongLong_t)(elapsed / m_Speed * newSpeed);
      } else {
        const GOTime now = wxGetLocalTimeMillis();
        const double filePos = (now - m_Start).ToDouble() / m_Speed;

        m_Start = now - (wxLongLong_t)(filePos * newSpeed);
      }
    }
    m_Speed = newSpeed;
    m_TempoPercent = percent;
    UpdateTempoDisplay();

    if (m_IsPlaying && !m_Pause) {
      // reschedule the next event with the new tempo
      r_timer.DeleteTimer(this);
      HandleTimer();
    }
  }
}

GOEnclosure *GOMidiPlayer::GetEnclosure(const wxString &name, bool is_panel) {
  return NULL;
}

GOLabelControl *GOMidiPlayer::GetLabelControl(
  const wxString &name, bool is_panel) {
  if (is_panel)
    return NULL;

  if (name == wxT("MidiPlayerLabel"))
    return &m_PlayingTime;
  if (name == wxT("MidiPlayerTempoLabel"))
    return &m_TempoLabel;
  return NULL;
}

void GOMidiPlayer::Cleanup() {
  StopPlaying();
  p_midi = nullptr;
}
