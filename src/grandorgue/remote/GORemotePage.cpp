/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GORemotePage.h"

const char *const GO_REMOTE_MANIFEST = R"GOJSON({
  "name": "Mando del órgano",
  "short_name": "Órgano",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#16130f",
  "theme_color": "#16130f",
  "icons": [{"src": "/icon.svg", "sizes": "any", "type": "image/svg+xml"}]
})GOJSON";

const char *const GO_REMOTE_ICON = R"GOSVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
<rect width="64" height="64" rx="14" fill="#16130f"/>
<g fill="#d9a441">
<rect x="10" y="22" width="6" height="30" rx="3"/>
<rect x="19" y="14" width="6" height="38" rx="3"/>
<rect x="28" y="8" width="8" height="44" rx="4"/>
<rect x="39" y="14" width="6" height="38" rx="3"/>
<rect x="48" y="22" width="6" height="30" rx="3"/>
</g></svg>)GOSVG";

const char *const GO_REMOTE_PAGE = R"GOHTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#16130f">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<link rel="manifest" href="/manifest.json">
<link rel="icon" href="/icon.svg">
<title>Mando del órgano</title>
<style>
:root {
  --bg: #16130f; --panel: #221d17; --panel2: #2c261e; --line: #3a3228;
  --text: #f1e9dc; --muted: #a89a86; --gold: #d9a441; --gold2: #f0c060;
  --red: #c4553f; --green: #6fa86a;
}
* { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
html, body { margin: 0; background: var(--bg); color: var(--text);
  font: 17px/1.35 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
body { padding: env(safe-area-inset-top) 0 env(safe-area-inset-bottom); }
button { font: inherit; color: var(--text); background: var(--panel2);
  border: 1px solid var(--line); border-radius: 14px; padding: 12px;
  min-height: 52px; cursor: pointer; touch-action: manipulation; }
button:active { transform: scale(0.97); background: var(--line); }
button:disabled { opacity: 0.35; }
button.gold { background: var(--gold); color: #1b1408; border-color: var(--gold); font-weight: 600; }
button.on { background: var(--gold); color: #1b1408; border-color: var(--gold2); font-weight: 700; }
header { display: flex; align-items: center; gap: 10px; padding: 12px 16px; }
header h1 { font-size: 18px; margin: 0; flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.dot { width: 10px; height: 10px; border-radius: 50%; background: var(--red); flex: none; }
.dot.ok { background: var(--green); }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 18px;
  margin: 0 12px 12px; padding: 14px; }
.now { font-size: 20px; font-weight: 600; margin-bottom: 2px; overflow-wrap: anywhere; }
.status { color: var(--muted); font-size: 15px; min-height: 20px; }
.row { display: flex; gap: 8px; align-items: center; }
.row > * { flex: 1; }
.transport { margin-top: 12px; }
.transport button { font-size: 24px; min-height: 64px; }
.transport .main { flex: 1.6; }
input[type=range] { width: 100%; height: 36px; accent-color: var(--gold); margin: 6px 0 0; }
.times { display: flex; justify-content: space-between; color: var(--muted); font-size: 14px; font-variant-numeric: tabular-nums; }
.fade { width: 100%; margin-top: 10px; min-height: 58px; font-size: 18px; }
.label { color: var(--muted); font-size: 14px; flex: none; min-width: 74px; }
.value { text-align: center; font-size: 20px; font-weight: 600; font-variant-numeric: tabular-nums; flex: 0 0 76px; }
.mt { margin-top: 10px; }
.tabs { display: flex; gap: 8px; margin: 0 12px 12px; }
.tabs button { flex: 1; min-height: 46px; }
.list { list-style: none; margin: 0; padding: 0; }
.list li { display: flex; align-items: center; gap: 8px; border-bottom: 1px solid var(--line); }
.list li:last-child { border-bottom: none; }
.list .name { flex: 1; padding: 14px 4px; cursor: pointer; overflow-wrap: anywhere; }
.list li.sel .name { color: var(--gold2); font-weight: 600; }
.list li.loaded .name::before { content: "♪ "; color: var(--gold); }
.list button { min-height: 44px; width: 52px; flex: none; padding: 6px; }
.switch { display: flex; align-items: center; gap: 10px; padding: 8px 0; }
.switch input { width: 26px; height: 26px; accent-color: var(--gold); }
.grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; }
.grid button { min-height: 54px; font-size: 19px; padding: 6px; }
.seq { font-size: 30px; font-weight: 700; text-align: center; font-variant-numeric: tabular-nums; flex: 0 0 110px; }
h2 { font-size: 15px; color: var(--muted); font-weight: 600; margin: 0 0 10px; text-transform: uppercase; letter-spacing: 0.06em; }
.hidden { display: none !important; }
#pinScreen { padding: 40px 24px; text-align: center; }
#pinScreen input { font-size: 32px; width: 100%; max-width: 260px; text-align: center; padding: 12px;
  border-radius: 14px; border: 1px solid var(--line); background: var(--panel); color: var(--text);
  letter-spacing: 0.3em; margin: 18px 0; }
#pinScreen button { width: 100%; max-width: 260px; }
.error { color: var(--red); min-height: 22px; }
.muted { color: var(--muted); font-size: 14px; }
</style>
</head>
<body>

<div id="pinScreen" class="hidden">
  <h1>Mando del órgano</h1>
  <p class="muted">Escribe el PIN que aparece en Grand Orgue,<br>en Audio/MIDI → Mando a distancia.</p>
  <input id="pinInput" type="password" inputmode="numeric" autocomplete="off" placeholder="PIN">
  <div class="error" id="pinError"></div>
  <button class="gold" id="pinOk">Entrar</button>
</div>

<div id="app" class="hidden">
  <header>
    <span class="dot" id="dot"></span>
    <h1 id="organ">Órgano</h1>
  </header>

  <div class="card">
    <div class="now" id="now">Ninguna pieza cargada</div>
    <div class="status" id="status"></div>
    <input type="range" id="timeline" min="0" max="1000" value="0">
    <div class="times"><span id="pos">0:00</span><span id="len">0:00</span></div>
    <div class="row transport">
      <button id="prev" aria-label="Anterior">⏮</button>
      <button id="playpause" class="main gold" aria-label="Reproducir">▶</button>
      <button id="stop" aria-label="Detener">⏹</button>
      <button id="next" aria-label="Siguiente">⏭</button>
    </div>
    <button class="fade" id="fade">Terminar suavemente</button>
  </div>

  <div class="card">
    <div class="row">
      <span class="label">Volumen</span>
      <button id="volDown">−</button>
      <span class="value" id="volume">0 dB</span>
      <button id="volUp">+</button>
    </div>
    <div class="row mt">
      <span class="label">Tempo</span>
      <button id="tempoDown">−</button>
      <span class="value" id="tempo">100%</span>
      <button id="tempoUp">+</button>
    </div>
    <div class="row mt">
      <span class="label"></span>
      <button id="tempo100">Tempo normal</button>
    </div>
  </div>

  <div class="tabs">
    <button id="tabList" class="on">Lista</button>
    <button id="tabCombi">Combinaciones</button>
  </div>

  <div class="card" id="listCard">
    <label class="switch"><input type="checkbox" id="auto"> Pasar sola a la siguiente pieza</label>
    <ul class="list" id="list"></ul>
    <p class="muted hidden" id="emptyList">No hay piezas en la carpeta de la lista.</p>
    <button class="mt" id="refresh" style="width:100%">Actualizar la lista</button>
  </div>

  <div id="combiCard" class="hidden">
    <div class="card">
      <h2>Secuenciador</h2>
      <div class="row">
        <button id="seqPrev" style="font-size:24px">◀</button>
        <span class="seq" id="seq">—</span>
        <button id="seqNext" style="font-size:24px">▶</button>
      </div>
      <button class="mt" id="seqHome" style="width:100%">Volver al principio</button>
    </div>
    <div class="card">
      <h2>Generales</h2>
      <div class="row">
        <button id="bankPrev">◀ Banco</button>
        <span class="seq" id="bank" style="font-size:24px">A</span>
        <button id="bankNext">Banco ▶</button>
      </div>
      <div class="grid mt" id="generals"></div>
      <div class="row mt">
        <button id="moreGenerals">Ver más</button>
        <button id="gc">Anular todo</button>
      </div>
    </div>
    <div class="card hidden" id="organGeneralsCard">
      <h2>Combinaciones del órgano</h2>
      <div class="grid" id="organGenerals" style="grid-template-columns: repeat(2, 1fr)"></div>
    </div>
  </div>
  <p class="muted" style="text-align:center; margin: 4px 0 20px">
    <a href="#" id="logout" style="color:var(--muted)">Cambiar PIN</a>
  </p>
</div>

<script>
(function () {
  "use strict";
  var pin = "";
  try { pin = localStorage.getItem("goPin") || ""; } catch (e) {}
  var state = null;
  var filesKey = "";
  var generalsShown = 10;
  var seeking = false;
  var busy = false;
  var $ = function (id) { return document.getElementById(id); };

  function fmt(ms) {
    var s = Math.max(0, Math.floor(ms / 1000));
    return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
  }
  function realMs(fileMs) {
    var tempo = state && state.tempo ? state.tempo : 100;
    return fileMs * 100 / tempo;
  }
  function showPin(message) {
    $("app").classList.add("hidden");
    $("pinScreen").classList.remove("hidden");
    $("pinError").textContent = message || "";
    $("pinInput").value = "";
  }
  function showApp() {
    $("pinScreen").classList.add("hidden");
    $("app").classList.remove("hidden");
  }

  function request(path, params) {
    var query = "pin=" + encodeURIComponent(pin);
    for (var k in params) query += "&" + k + "=" + encodeURIComponent(params[k]);
    return fetch(path + "?" + query, {method: path === "/api/cmd" ? "POST" : "GET", cache: "no-store"})
      .then(function (r) {
        if (r.status === 401) { var err = new Error("pin"); err.pin = true; throw err; }
        return r.json();
      });
  }
  function handle(promise) {
    return promise.then(function (s) {
      if (s && !s.error) { state = s; render(); }
      $("dot").classList.add("ok");
    }).catch(function (e) {
      if (e && e.pin) {
        var hadPin = !!pin;
        pin = ""; // stop polling until a new PIN is entered
        showPin(hadPin ? "El PIN no es correcto" : "");
      }
      else $("dot").classList.remove("ok");
    });
  }
  function cmd(action, params) {
    params = params || {};
    params.a = action;
    if (navigator.vibrate) { try { navigator.vibrate(15); } catch (e) {} }
    return handle(request("/api/cmd", params));
  }
  function poll() {
    if (busy || !pin) return;
    busy = true;
    handle(request("/api/state", {})).then(function () { busy = false; });
  }

  function renderList() {
    var key = JSON.stringify(state.files) + "|" + state.selected + "|" + state.loaded;
    if (key === filesKey) return;
    filesKey = key;
    var list = $("list");
    list.innerHTML = "";
    $("emptyList").classList.toggle("hidden", state.files.length > 0);
    state.files.forEach(function (name, i) {
      var li = document.createElement("li");
      if (i === state.selected) li.className = "sel";
      if (i === state.loaded) li.className += " loaded";
      var span = document.createElement("span");
      span.className = "name";
      span.textContent = name;
      span.onclick = function () { cmd("select", {i: i}); };
      var play = document.createElement("button");
      play.textContent = "▶";
      play.setAttribute("aria-label", "Tocar " + name);
      play.onclick = function () { cmd("play", {i: i}); };
      li.appendChild(span);
      li.appendChild(play);
      list.appendChild(li);
    });
  }

  function renderGenerals() {
    var grid = $("generals");
    if (grid.children.length !== generalsShown) {
      grid.innerHTML = "";
      for (var n = 1; n <= generalsShown; n++) {
        (function (n) {
          var b = document.createElement("button");
          b.textContent = n;
          b.onclick = function () { cmd("general", {n: n}); };
          grid.appendChild(b);
        })(n);
      }
    }
    for (var i = 0; i < grid.children.length; i++)
      grid.children[i].classList.toggle("on", !!(state.generals && state.generals[i]));
    $("moreGenerals").textContent = generalsShown === 10 ? "Ver más (hasta 50)" : "Ver menos";

    var og = state.organGenerals || [];
    $("organGeneralsCard").classList.toggle("hidden", og.length === 0);
    var ogrid = $("organGenerals");
    if (ogrid.children.length !== og.length) {
      ogrid.innerHTML = "";
      og.forEach(function (g, i) {
        var b = document.createElement("button");
        b.onclick = function () { cmd("ogeneral", {i: i}); };
        ogrid.appendChild(b);
      });
    }
    og.forEach(function (g, i) {
      ogrid.children[i].textContent = g.name;
      ogrid.children[i].classList.toggle("on", g.on);
    });
  }

  function render() {
    showApp();
    $("organ").textContent = state.organ || "No hay ningún órgano cargado";
    var hasOrgan = !!state.organ;
    var loadedName = state.loaded >= 0 ? state.files[state.loaded] : "";
    $("now").textContent = loadedName || "Ninguna pieza cargada";
    var status = "";
    if (!hasOrgan) status = "Carga un órgano en Grand Orgue";
    else if (state.fading) status = "Terminando suavemente…";
    else if (state.paused) status = "En pausa";
    else if (state.playing) status = "Sonando";
    else if (loadedName) status = "Preparada";
    if (state.playing && state.selected >= 0 && state.selected !== state.loaded)
      status += " · Siguiente: " + state.files[state.selected];
    $("status").textContent = status;

    if (!seeking) {
      $("timeline").value = state.length > 0 ? Math.round(state.position / state.length * 1000) : 0;
      $("pos").textContent = fmt(realMs(state.position));
    }
    $("len").textContent = fmt(realMs(state.length));
    $("timeline").disabled = !(state.length > 0);

    var playing = state.playing && !state.paused &&
      (state.selected < 0 || state.selected === state.loaded);
    $("playpause").textContent = playing ? "⏸" : "▶";
    $("playpause").setAttribute("aria-label", playing ? "Pausa" : "Reproducir");
    $("playpause").disabled = !hasOrgan || state.files.length === 0;
    $("stop").disabled = !state.playing;
    $("fade").disabled = !state.playing || state.paused || state.fading;
    $("fade").textContent = state.fading ? "Terminando…" : "Terminar suavemente";
    $("prev").disabled = !hasOrgan || state.selected <= 0;
    $("next").disabled = !hasOrgan || state.selected < 0 || state.selected >= state.files.length - 1;

    $("volume").textContent = state.volume + " dB";
    $("tempo").textContent = state.tempo + "%";
    $("auto").checked = !!state.autoAdvance;
    $("seq").textContent = state.sequencer || "—";
    $("bank").textContent = state.bank || "—";
    renderList();
    renderGenerals();
  }

  // the buttons
  $("prev").onclick = function () { cmd("prev"); };
  $("next").onclick = function () { cmd("next"); };
  $("playpause").onclick = function () { cmd("playpause"); };
  $("stop").onclick = function () { cmd("stop"); };
  $("fade").onclick = function () { cmd("fade"); };
  $("volDown").onclick = function () { if (state) cmd("vol", {v: state.volume - 1}); };
  $("volUp").onclick = function () { if (state) cmd("vol", {v: state.volume + 1}); };
  $("tempoDown").onclick = function () { cmd("tempo", {d: -5}); };
  $("tempoUp").onclick = function () { cmd("tempo", {d: 5}); };
  $("tempo100").onclick = function () { cmd("tempo100"); };
  $("auto").onchange = function () { cmd("auto", {on: this.checked ? 1 : 0}); };
  $("refresh").onclick = function () { filesKey = ""; cmd("refresh"); };
  $("seqPrev").onclick = function () { cmd("seqprev"); };
  $("seqNext").onclick = function () { cmd("seqnext"); };
  $("seqHome").onclick = function () { cmd("seqhome"); };
  $("bankPrev").onclick = function () { cmd("bankprev"); };
  $("bankNext").onclick = function () { cmd("banknext"); };
  $("gc").onclick = function () { cmd("gc"); };
  $("moreGenerals").onclick = function () {
    generalsShown = generalsShown === 10 ? 50 : 10;
    if (state) renderGenerals();
  };
  $("tabList").onclick = function () {
    $("tabList").classList.add("on"); $("tabCombi").classList.remove("on");
    $("listCard").classList.remove("hidden"); $("combiCard").classList.add("hidden");
  };
  $("tabCombi").onclick = function () {
    $("tabCombi").classList.add("on"); $("tabList").classList.remove("on");
    $("combiCard").classList.remove("hidden"); $("listCard").classList.add("hidden");
  };

  // the timeline: show the time while dragging, jump when released
  var timeline = $("timeline");
  timeline.addEventListener("input", function () {
    seeking = true;
    if (state) $("pos").textContent = fmt(realMs(state.length * timeline.value / 1000));
  });
  timeline.addEventListener("change", function () {
    if (state) cmd("seek", {ms: Math.round(state.length * timeline.value / 1000)})
      .then(function () { seeking = false; });
    else seeking = false;
  });

  // the PIN
  function login() {
    pin = $("pinInput").value.trim();
    try { localStorage.setItem("goPin", pin); } catch (e) {}
    $("pinError").textContent = "";
    poll();
  }
  $("pinOk").onclick = login;
  $("pinInput").addEventListener("keydown", function (e) { if (e.key === "Enter") login(); });
  $("logout").onclick = function (e) {
    e.preventDefault();
    pin = "";
    try { localStorage.removeItem("goPin"); } catch (e2) {}
    showPin("");
  };

  if (pin) poll(); else showPin("");
  setInterval(poll, 1000);
  document.addEventListener("visibilitychange", function () { if (!document.hidden) poll(); });
})();
</script>
</body>
</html>
)GOHTML";
