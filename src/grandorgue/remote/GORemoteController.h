/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOREMOTECONTROLLER_H
#define GOREMOTECONTROLLER_H

#include <map>
#include <string>

class GOMidiPlaylistDialog;
class GOOrganController;

/**
 * Executes the remote control commands and describes the state as JSON.
 * Called by GORemoteServer in the main thread.
 *
 * Commands (parameter a):
 * - state: nothing, only the state
 * - select (i), play (i, optional), playpause, stop, prev, next, fade,
 *   tempo (d), tempo100, seek (ms), auto (on=0/1), refresh: the playlist
 * - vol (v): the volume in dB
 * - seqprev, seqnext, seqhome: the combination sequencer
 * - general (n = 1..50), bankprev, banknext, gc: the banked generals
 * - ogeneral (i): the generals defined by the organ
 */
class GORemoteController {
public:
  /** The services needed from the main window */
  class Host {
  public:
    virtual ~Host() {}

    /** @return the loaded organ or nullptr */
    virtual GOOrganController *GetOrganController() = 0;
    /** @return the playlist window, created hidden if necessary */
    virtual GOMidiPlaylistDialog &EnsureMidiPlaylist() = 0;
    /** @return the volume setting in dB */
    virtual int GetVolume() = 0;
    /** Sets the volume like the volume control of the main window */
    virtual void SetVolume(int volume) = 0;
  };

private:
  Host &r_host;

  void Execute(
    const std::string &action,
    const std::map<std::string, std::string> &params);
  std::string GetStateJson();

public:
  GORemoteController(Host &host) : r_host(host) {}

  /**
   * Executes a command
   * @param action the command name
   * @param params the command parameters
   * @return the state after the command as JSON
   */
  std::string Handle(
    const std::string &action,
    const std::map<std::string, std::string> &params);
};

#endif
