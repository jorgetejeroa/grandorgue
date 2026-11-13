/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GORemoteController.h"

#include <cstdio>
#include <cstdlib>

#include <wx/filename.h>

#include "combinations/control/GOGeneralButtonControl.h"
#include "control/GOButtonControl.h"
#include "control/GOLabelControl.h"
#include "gui/dialogs/GOMidiPlaylistDialog.h"
#include "midi/GOMidiPlayer.h"

#include "GOOrganController.h"

static const unsigned N_BANKED_GENERALS = 50;

namespace {

/** A minimal JSON writer */
class JsonWriter {
private:
  std::string m_text;
  bool m_IsFirst = true;

  void Separator() {
    if (!m_IsFirst)
      m_text += ',';
    m_IsFirst = false;
  }

public:
  static std::string quote(const std::string &s) {
    std::string result = "\"";

    for (unsigned char c : s) {
      if (c == '"' || c == '\\') {
        result += '\\';
        result += (char)c;
      } else if (c < 0x20) {
        char buf[8];

        snprintf(buf, sizeof(buf), "\\u%04x", c);
        result += buf;
      } else
        result += (char)c;
    }
    result += '"';
    return result;
  }

  static std::string quote(const wxString &s) {
    return quote(std::string(s.utf8_str()));
  }

  void Key(const char *key) {
    Separator();
    m_text += quote(std::string(key));
    m_text += ':';
    m_IsFirst = true;
  }
  void BeginObject() {
    Separator();
    m_text += '{';
    m_IsFirst = true;
  }
  void EndObject() {
    m_text += '}';
    m_IsFirst = false;
  }
  void BeginArray() {
    Separator();
    m_text += '[';
    m_IsFirst = true;
  }
  void EndArray() {
    m_text += ']';
    m_IsFirst = false;
  }
  void Raw(const std::string &value) {
    Separator();
    m_text += value;
  }
  void Value(const wxString &value) { Raw(quote(value)); }
  void Value(bool value) { Raw(value ? "true" : "false"); }
  void Value(long long value) { Raw(std::to_string(value)); }
  void Null() { Raw("null"); }

  const std::string &GetText() const { return m_text; }
};

} // namespace

static long get_int(
  const std::map<std::string, std::string> &params,
  const char *name,
  long defaultValue) {
  const auto it = params.find(name);

  return it != params.end() ? strtol(it->second.c_str(), nullptr, 10)
                            : defaultValue;
}

static void push_button(GOOrganController *pController, const wxString &name) {
  GOButtonControl *pButton
    = pController ? pController->GetButtonControl(name) : nullptr;

  if (pButton)
    pButton->Push();
}

void GORemoteController::Execute(
  const std::string &action, const std::map<std::string, std::string> &params) {
  GOOrganController *pController = r_host.GetOrganController();
  GOMidiPlaylistDialog &playlist = r_host.EnsureMidiPlaylist();

  if (action == "select")
    playlist.SelectPiece((int)get_int(params, "i", -1));
  else if (action == "play")
    playlist.PlayPiece((int)get_int(params, "i", playlist.GetSelectedIndex()));
  else if (action == "playpause") {
    GOMidiPlayer *pPlayer
      = pController ? pController->GetMidiPlayer() : nullptr;

    if (
      pPlayer && pPlayer->IsPlaying() && !pPlayer->IsPaused()
      && playlist.GetLoadedIndex() == playlist.GetSelectedIndex())
      playlist.TogglePause();
    else
      playlist.PlayOrContinue();
  } else if (action == "stop")
    playlist.Stop();
  else if (action == "prev")
    playlist.Previous();
  else if (action == "next")
    playlist.Next();
  else if (action == "fade")
    playlist.FadeOut();
  else if (action == "tempo")
    playlist.ChangeTempo((int)get_int(params, "d", 0));
  else if (action == "tempo100")
    playlist.ResetTempo();
  else if (action == "seek")
    playlist.SeekTo((double)get_int(params, "ms", 0));
  else if (action == "auto")
    playlist.SetAutoAdvance(get_int(params, "on", 0) != 0);
  else if (action == "refresh")
    playlist.RefreshList();
  else if (action == "vol") {
    long volume = get_int(params, "v", r_host.GetVolume());

    if (volume < -120)
      volume = -120;
    if (volume > 20)
      volume = 20;
    r_host.SetVolume((int)volume);
  } else if (action == "seqprev")
    push_button(pController, wxT("Prev"));
  else if (action == "seqnext")
    push_button(pController, wxT("Next"));
  else if (action == "seqhome")
    push_button(pController, wxT("Home"));
  else if (action == "gc")
    push_button(pController, wxT("GC"));
  else if (action == "bankprev")
    push_button(pController, wxT("GeneralPrev"));
  else if (action == "banknext")
    push_button(pController, wxT("GeneralNext"));
  else if (action == "general") {
    const long n = get_int(params, "n", 0);

    if (n >= 1 && n <= (long)N_BANKED_GENERALS)
      push_button(pController, wxString::Format(wxT("General%02ld"), n));
  } else if (action == "ogeneral" && pController) {
    const long i = get_int(params, "i", -1);

    if (i >= 0 && i < (long)pController->GetGeneralCount())
      pController->GetGeneral((unsigned)i)->Push();
  }
}

std::string GORemoteController::GetStateJson() {
  GOOrganController *pController = r_host.GetOrganController();
  GOMidiPlaylistDialog &playlist = r_host.EnsureMidiPlaylist();
  GOMidiPlayer *pPlayer = pController ? pController->GetMidiPlayer() : nullptr;
  JsonWriter json;

  json.BeginObject();
  json.Key("organ");
  if (pController)
    json.Value(pController->GetOrganName());
  else
    json.Null();

  // the playlist
  json.Key("files");
  json.BeginArray();
  for (const wxString &path : playlist.GetFiles())
    json.Value(wxFileName(path).GetName());
  json.EndArray();
  json.Key("selected");
  json.Value((long long)playlist.GetSelectedIndex());
  json.Key("loaded");
  json.Value((long long)playlist.GetLoadedIndex());
  json.Key("playing");
  json.Value(pPlayer && pPlayer->IsPlaying());
  json.Key("paused");
  json.Value(pPlayer && pPlayer->IsPaused());
  json.Key("position");
  json.Value((long long)(pPlayer ? pPlayer->GetPosition() : 0));
  json.Key("length");
  json.Value((long long)(pPlayer && pPlayer->IsLoaded()
                           ? pPlayer->GetLength().GetValue()
                           : 0));
  json.Key("tempo");
  json.Value((long long)(pPlayer ? pPlayer->GetTempoPercent() : 100));
  json.Key("autoAdvance");
  json.Value(playlist.IsAutoAdvance());
  json.Key("fading");
  json.Value(playlist.IsFading());

  // the volume
  json.Key("volume");
  json.Value((long long)r_host.GetVolume());

  // the combinations
  GOLabelControl *pSeqLabel
    = pController ? pController->GetLabel(wxT("Label")) : nullptr;
  GOLabelControl *pBankLabel
    = pController ? pController->GetLabel(wxT("GeneralLabel")) : nullptr;

  json.Key("sequencer");
  json.Value(pSeqLabel ? pSeqLabel->GetContent() : wxString());
  json.Key("bank");
  json.Value(pBankLabel ? pBankLabel->GetContent() : wxString());
  json.Key("generals");
  json.BeginArray();
  for (unsigned n = 1; n <= N_BANKED_GENERALS && pController; n++) {
    GOButtonControl *pButton
      = pController->GetButtonControl(wxString::Format(wxT("General%02u"), n));

    json.Value(pButton && pButton->IsEngaged());
  }
  json.EndArray();
  json.Key("organGenerals");
  json.BeginArray();
  if (pController) {
    for (unsigned n = pController->GetGeneralCount(), i = 0; i < n; i++) {
      GOGeneralButtonControl *pGeneral = pController->GetGeneral(i);

      json.BeginObject();
      json.Key("name");
      json.Value(pGeneral->GetName());
      json.Key("on");
      json.Value(pGeneral->IsEngaged());
      json.EndObject();
    }
  }
  json.EndArray();
  json.EndObject();
  return json.GetText();
}

std::string GORemoteController::Handle(
  const std::string &action, const std::map<std::string, std::string> &params) {
  if (action != "state")
    Execute(action, params);
  return GetStateJson();
}
