/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOREMOTESERVER_H
#define GOREMOTESERVER_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wx/string.h>

class wxEvtHandler;

/**
 * A small web server that lets a phone or a tablet in the same network
 * control GrandOrgue with a web page. It serves the page and a JSON API:
 * - GET /api/state?pin=... returns the state
 * - POST /api/cmd?pin=...&a=<action>&... executes a command and returns the
 *   state
 * The requests are handled in the server threads, but the handler is always
 * called in the main (GUI) thread, so it may use the organ freely.
 */
class GORemoteServer {
public:
  /**
   * Handles an API request in the main thread
   * @param action the command or "state"
   * @param params all the request parameters
   * @return the JSON answer
   */
  using Handler = std::function<std::string(
    const std::string &action,
    const std::map<std::string, std::string> &params)>;

private:
  struct Impl;

  std::unique_ptr<Impl> o_impl;

public:
  /**
   * @param mainThreadHandler the event handler used to call handler in the
   *   main thread. Pending calls are dropped if it is destroyed
   * @param handler the API request handler
   */
  GORemoteServer(wxEvtHandler &mainThreadHandler, Handler handler);
  ~GORemoteServer();

  /**
   * Starts listening on all network interfaces. Stops a running server first
   * @param port the TCP port
   * @param pin the PIN that every API request must carry
   * @return false if the port cannot be used
   */
  bool Start(unsigned port, const wxString &pin);
  /** Stops the server. Safe to call when it is not running */
  void Stop();
  bool IsRunning() const;

  /** @return the IPv4 addresses of this computer, except the loopback one */
  static std::vector<wxString> GetLocalAddresses();
};

#endif
