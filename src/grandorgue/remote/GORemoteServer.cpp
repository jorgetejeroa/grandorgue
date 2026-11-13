/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

// httplib needs Windows 10 API and must be included before any windows.h
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "httplib/httplib.h"

#include "GORemoteServer.h"

#include <chrono>
#include <future>
#include <thread>

#include <wx/event.h>

#include "GORemotePage.h"

static const auto MAIN_THREAD_TIMEOUT = std::chrono::seconds(3);

struct GORemoteServer::Impl {
  wxEvtHandler &r_MainThreadHandler;
  Handler m_handler;
  std::unique_ptr<httplib::Server> o_server;
  std::thread m_thread;
  std::string m_pin;

  Impl(wxEvtHandler &mainThreadHandler, Handler handler)
    : r_MainThreadHandler(mainThreadHandler), m_handler(handler) {}

  /**
   * Calls the handler in the main thread and waits for the answer
   */
  std::string CallInMainThread(
    const std::string &action,
    const std::map<std::string, std::string> &params) {
    auto oPromise = std::make_shared<std::promise<std::string>>();
    std::future<std::string> answer = oPromise->get_future();
    Handler handler = m_handler;
    std::string result;

    r_MainThreadHandler.CallAfter([oPromise, handler, action, params]() {
      oPromise->set_value(handler(action, params));
    });
    result = "{\"error\":\"busy\"}";
    if (answer.wait_for(MAIN_THREAD_TIMEOUT) == std::future_status::ready) {
      try {
        result = answer.get();
      } catch (const std::future_error &) {
        // the main window has gone and dropped the call
      }
    }
    return result;
  }

  void HandleApi(
    const httplib::Request &req,
    httplib::Response &res,
    const std::string &defaultAction) {
    std::map<std::string, std::string> params;

    for (const auto &param : req.params)
      params[param.first] = param.second;

    const auto pinIt = params.find("pin");

    if (pinIt == params.end() || pinIt->second != m_pin) {
      res.status = 401;
      res.set_content("{\"error\":\"pin\"}", "application/json");
    } else {
      const auto actionIt = params.find("a");
      const std::string action
        = actionIt != params.end() ? actionIt->second : defaultAction;

      res.set_content(
        CallInMainThread(action, params), "application/json; charset=utf-8");
    }
    res.set_header("Cache-Control", "no-store");
  }

  void SetupRoutes() {
    o_server->Get("/", [](const httplib::Request &, httplib::Response &res) {
      res.set_content(GO_REMOTE_PAGE, "text/html; charset=utf-8");
      res.set_header("Cache-Control", "no-store");
    });
    o_server->Get(
      "/manifest.json", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(GO_REMOTE_MANIFEST, "application/manifest+json");
      });
    o_server->Get(
      "/icon.svg", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(GO_REMOTE_ICON, "image/svg+xml");
      });
    o_server->Get(
      "/api/state",
      [this](const httplib::Request &req, httplib::Response &res) {
        HandleApi(req, res, "state");
      });
    o_server->Post(
      "/api/cmd", [this](const httplib::Request &req, httplib::Response &res) {
        HandleApi(req, res, "state");
      });
  }
};

GORemoteServer::GORemoteServer(wxEvtHandler &mainThreadHandler, Handler handler)
  : o_impl(std::make_unique<Impl>(mainThreadHandler, handler)) {}

GORemoteServer::~GORemoteServer() { Stop(); }

bool GORemoteServer::Start(unsigned port, const wxString &pin) {
  bool isStarted = false;

  Stop();
  o_impl->m_pin = std::string(pin.utf8_str());
  o_impl->o_server = std::make_unique<httplib::Server>();
  o_impl->SetupRoutes();
  if (o_impl->o_server->bind_to_port("0.0.0.0", (int)port)) {
    httplib::Server *pServer = o_impl->o_server.get();

    o_impl->m_thread
      = std::thread([pServer]() { pServer->listen_after_bind(); });
    isStarted = true;
  } else
    o_impl->o_server.reset();
  return isStarted;
}

void GORemoteServer::Stop() {
  if (o_impl->o_server) {
    o_impl->o_server->stop();
    if (o_impl->m_thread.joinable())
      o_impl->m_thread.join();
    o_impl->o_server.reset();
  }
}

bool GORemoteServer::IsRunning() const { return (bool)o_impl->o_server; }

std::vector<wxString> GORemoteServer::GetLocalAddresses() {
  std::vector<wxString> addresses;
  char hostName[256];

#ifdef _WIN32
  WSADATA wsaData;
  const bool isWsaStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#endif
  if (gethostname(hostName, sizeof(hostName)) == 0) {
    addrinfo hints;
    addrinfo *pResult = nullptr;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostName, nullptr, &hints, &pResult) == 0) {
      for (addrinfo *pAddr = pResult; pAddr; pAddr = pAddr->ai_next) {
        char text[INET_ADDRSTRLEN];
        const sockaddr_in *pIn = (const sockaddr_in *)pAddr->ai_addr;

        if (
          inet_ntop(AF_INET, &pIn->sin_addr, text, sizeof(text))
          && strncmp(text, "127.", 4) != 0) {
          const wxString address(text);
          bool isNew = true;

          for (const wxString &known : addresses)
            if (known == address)
              isNew = false;
          if (isNew)
            addresses.push_back(address);
        }
      }
      freeaddrinfo(pResult);
    }
  }
#ifdef _WIN32
  if (isWsaStarted)
    WSACleanup();
#endif
  return addresses;
}
