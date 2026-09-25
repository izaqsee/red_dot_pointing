"use strict";

(() => {
  const KEYS = ["pointerSensitivity", "pointerInvertX", "pointerInvertY", "wheelSensitivityX", "wheelSensitivityY", "wheelInvertX", "wheelInvertY"];
  const ACTION_KEYS = ["leftAction", "middleAction", "rightAction"];
  const ALL_KEYS = [...KEYS, ...ACTION_KEYS];
  const { validAction, actionLabel, createShortcutRecorder } =
    typeof module !== "undefined" && module.exports ? require("./shortcuts.js") : window.RedPointShortcuts;
  const RESPONSE_TIMEOUT_MS = 2000;
  const AUTO_PROBE_TIMEOUT_MS = 1200;
  const EDIT_DEBOUNCE_MS = 120;
  const MAX_RESPONSE_LINE = 2048;

  const isWheelSensitivity = key => key === "wheelSensitivityX" || key === "wheelSensitivityY";
  // Zero is OFF; each 100 slider steps spans one decade (0.001 to 10).
  function wheelSensitivityFromSlider(position) {
    if (!Number.isFinite(position) || position < 0 || position > 401) return NaN;
    return position === 0 ? 0 : Number((0.001 * 10 ** ((position - 1) / 100)).toFixed(6));
  }
  function wheelSliderFromSensitivity(value) {
    return value <= 0 ? 0 : Math.max(1, Math.min(401, 1 + 100 * Math.log10(value / 0.001)));
  }

  // Protocol: independent of the browser, SerialPort and DOM.
  function validConfig(config) {
    return config && typeof config === "object" &&
      ["pointerSensitivity", "wheelSensitivityX", "wheelSensitivityY"].every(key =>
        typeof config[key] === "number" && Number.isFinite(config[key]) &&
        config[key] >= 0 && config[key] <= 10) &&
      ["pointerInvertX", "pointerInvertY", "wheelInvertX", "wheelInvertY"].every(key => typeof config[key] === "boolean") &&
      (ACTION_KEYS.every(key => !Object.hasOwn(config, key)) || ACTION_KEYS.every(key => validAction(config[key])));
  }

  function parseLine(line) {
    if (line.startsWith("@DEBUG ")) return { kind: "debug" };
    if (!line.startsWith("@CONFIG ")) return { kind: "unknown" };
    try {
      const response = JSON.parse(line.slice(8));
      if (response && response.ok === false && typeof response.error === "string") {
        return { kind: "config", response };
      }
      if (response && response.ok === true &&
          (response.command === "PING" || (["GET", "SET", "RESET", "SAVE"].includes(response.command) && validConfig(response.config)))) {
        return { kind: "config", response };
      }
    } catch { /* Malformed lines are isolated; keep receiving. */ }
    return { kind: "invalid" };
  }

  function createLineReader(onLine) {
    let line = "";
    let overflow = false;
    return chunk => {
      for (const char of chunk) {
        if (char === "\r" || char === "\n") {
          if (!overflow && line) onLine(line);
          line = "";
          overflow = false;
        } else if (!overflow) {
          if (line.length >= MAX_RESPONSE_LINE) {
            line = "";
            overflow = true;
          } else line += char;
        }
      }
    };
  }

  function protocolError(code) {
    const error = new Error(code);
    error.code = code;
    return error;
  }

  function createProtocol(send, timeoutMs = RESPONSE_TIMEOUT_MS) {
    let pending = null;
    let closed = false;
    let needsSync = false;
    function finish(error, config) {
      if (!pending) return;
      const request = pending;
      pending = null;
      clearTimeout(request.timer);
      if (error) request.reject(error);
      else request.resolve(config);
    }
    const accept = createLineReader(line => {
      const parsed = parseLine(line);
      if (parsed.kind !== "config" || !pending) return;
      const response = parsed.response;
      // During resync, a late SET/RESET/SAVE/error must not complete GET.
      if (!response.ok) {
        if (!pending.resync) finish(protocolError(response.error));
      } else if (response.command === pending.command) {
        needsSync = false;
        finish(null, response.command === "PING" ? undefined : Object.fromEntries(ALL_KEYS.filter(key => Object.hasOwn(response.config, key)).map(key => [key, response.config[key]])));
      }
    });
    return {
      accept,
      isIdle: () => !closed && !pending && !needsSync,
      request(command, { resync = false } = {}) {
        if (closed) return Promise.reject(protocolError("DISCONNECTED"));
        if (pending) return Promise.reject(protocolError("BUSY"));
        if (needsSync && !(resync && command === "GET")) {
          return Promise.reject(protocolError("RESYNC_REQUIRED"));
        }
        return new Promise((resolve, reject) => {
          const request = { command: command.split(" ")[0], resync, resolve, reject };
          pending = request;
          request.timer = setTimeout(() => {
            if (pending !== request) return;
            needsSync = true;
            finish(protocolError("TIMEOUT"));
          }, timeoutMs);
          // Also catch write failures; do not let a stalled write bypass the timeout.
          Promise.resolve().then(() => send(`${resync ? "\n" : ""}${command}\n`)).catch(error => {
            if (pending === request) {
              needsSync = true;
              finish(protocolError(`WRITE_FAILED: ${error.message}`));
            }
          });
        });
      },
      close() {
        closed = true;
        finish(protocolError("DISCONNECTED"));
      }
    };
  }

  // No queue: a heartbeat tick is expendable; user work is not.
  function createHeartbeat(protocol, canSend, onError, onIdle, intervalMs = 2000) {
    let stopped = false;
    let supported = null;
    let inFlight = false;
    let timer = null;
    async function tick() {
      if (stopped || supported === false || inFlight || !canSend() || !protocol.isIdle()) return;
      inFlight = true;
      try {
        await protocol.request("PING");
        if (!stopped) supported = true;
      } catch (error) {
        if (!stopped) {
          if (error.code === "UNKNOWN_COMMAND") {
            supported = false;
            clearInterval(timer);
          } else await onError(error);
        }
      } finally {
        inFlight = false;
        if (!stopped) onIdle();
      }
    }
    return {
      start() {
        if (stopped || timer !== null) return;
        // The first idle tick is the single capability probe after GET sync.
        timer = setInterval(() => { void tick(); }, intervalMs);
        void tick();
      },
      stop() { stopped = true; clearInterval(timer); timer = null; },
      isBusy: () => inFlight
    };
  }

  // Transport: byte streams only. A future HID transport can replace this section.
  function createSerialTransport(port, onText, onLost) {
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    let reader = null;
    let writer = null;
    let reading = null;
    let closing = false;
    let closePromise = null;
    return {
      start() {
        writer = port.writable.getWriter();
        reader = port.readable.getReader();
        reading = (async () => {
          try {
            while (!closing) {
              const { value, done } = await reader.read();
              if (done) break;
              if (value) onText(decoder.decode(value, { stream: true }));
            }
            if (!closing) onLost(new Error("Serial入力が終了しました。"));
          } catch (error) {
            if (!closing) onLost(error);
          } finally {
            reader.releaseLock();
            reader = null;
          }
        })();
      },
      async send(text) {
        if (closing || !writer) throw new Error("Serialポートが閉じています。");
        await writer.write(encoder.encode(text));
      },
      close() {
        if (closePromise) return closePromise;
        closing = true;
        closePromise = (async () => {
          // Cancel the blocking read and pending writes before releasing locks.
          await Promise.allSettled([reader?.cancel(), writer?.abort()]);
          await reading;
          if (writer) {
            writer.releaseLock();
            writer = null;
          }
          await port.close();
        })();
        return closePromise;
      }
    };
  }

  // HTTP uses the exact same framed protocol as Serial. No address assumptions.
  function createHttpTransport(fetchRequest, onText, timeoutMs = RESPONSE_TIMEOUT_MS) {
    let closed = false;
    let current = null;
    function cancel() {
      if (!current) return;
      current.cancelled = true;
      clearTimeout(current.timer);
      current.controller.abort();
      current = null;
    }
    return {
      start() {},
      async send(text) {
        if (closed) throw new Error("HTTP session is closed");
        cancel(); // A resync supersedes the previous timed-out HTTP request.
        const request = { controller: new AbortController(), cancelled: false, deadline: Date.now() + timeoutMs };
        current = request;
        request.timer = setTimeout(() => {
          request.cancelled = true;
          request.controller.abort();
        }, timeoutMs);
        try {
          const response = await fetchRequest("api/command", {
            method: "POST", headers: { "Content-Type": "text/plain", "Cache-Control": "no-store" },
            body: text, cache: "no-store", credentials: "same-origin", mode: "same-origin",
            redirect: "error", signal: request.controller.signal
          });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          const body = await response.text();
          if (body.length > MAX_RESPONSE_LINE) throw new Error("HTTP response too large");
          if (!closed && current === request && !request.cancelled && Date.now() < request.deadline) onText(body);
        } catch (error) {
          // The protocol owns timeout/resync. An abort must not turn timeout into WRITE_FAILED.
          if (!closed && current === request && !request.cancelled && Date.now() < request.deadline) throw error;
        } finally { clearTimeout(request.timer); }
      },
      close() { closed = true; cancel(); return Promise.resolve(); }
    };
  }

  function createHttpConnection(fetchRequest, timeoutMs = RESPONSE_TIMEOUT_MS) {
    const active = { kind: "http" };
    active.protocol = createProtocol(text => active.transport.send(text), timeoutMs);
    active.transport = createHttpTransport(fetchRequest, text => active.protocol.accept(text), timeoutMs);
    active.openAndSync = () => active.protocol.request("GET", { resync: true });
    active.close = () => {
      active.heartbeat?.stop();
      active.protocol.close();
      return active.transport.close();
    };
    return active;
  }

  function authorizedCandidates(ports) {
    return [...new Set(ports)].filter(port => {
      try { return port.getInfo().usbVendorId === 0x2e8a; }
      catch { return false; }
    });
  }

  // One owner for open, GET identity/sync and cleanup, for probes and sessions.
  function createDeviceConnection(port, onLost = () => {}, timeoutMs = RESPONSE_TIMEOUT_MS) {
    let opening = null;
    let opened = false;
    let closing = false;
    let lost = false;
    let closePromise = null;
    const active = { port };
    active.protocol = createProtocol(text => active.transport.send(text), timeoutMs);
    active.fail = error => {
      if (closing || lost) return;
      lost = true;
      active.protocol.close();
      onLost(error);
    };
    active.transport = createSerialTransport(port, text => active.protocol.accept(text), active.fail);
    active.openAndSync = async () => {
      opening = port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
      await opening;
      opened = true;
      if (closing || lost) throw protocolError("DISCONNECTED");
      active.transport.start();
      // The existing validated GET parser is also the identity check.
      const config = await active.protocol.request("GET", { resync: true });
      if (closing || lost) throw protocolError("DISCONNECTED");
      return config;
    };
    active.close = () => {
      if (closePromise) return closePromise;
      closing = true;
      active.heartbeat?.stop();
      active.protocol.close();
      closePromise = (async () => {
        // If unplug/cancel happens while open is pending, close only after it
        // settles. Never close a port whose open failed (another owner may use it).
        await opening?.catch(() => {});
        if (opened) await active.transport.close();
      })();
      return closePromise;
    };
    return active;
  }

  async function probeAuthorizedPorts(ports, createConnection = port => createDeviceConnection(port, undefined, AUTO_PROBE_TIMEOUT_MS), isCurrent = () => true) {
    const matches = [];
    for (const port of ports) {
      if (!isCurrent()) break;
      const active = createConnection(port);
      try {
        await active.openAndSync();
        if (isCurrent()) matches.push(port);
      } catch { /* A failed candidate does not prevent probing the next one. */ }
      finally {
        // A close failure aborts discovery: do not risk opening two ports.
        await active.close();
      }
    }
    return matches;
  }

  // UI: confirmed configuration comes only from a matched device response.
  function mount(document, serial, secureContext, { fetch: fetchRequest = document.defaultView?.fetch?.bind(document.defaultView) } = {}) {
    const byId = id => document.getElementById(id);
    const controls = Object.fromEntries(KEYS.map(key => [key, byId(key)]));
    const supported = Boolean(serial && secureContext);
    let httpMode = false;
    let session = null;
    let attempt = null;
    let autoConnectSuppressed = false;
    let connectionState = "disconnected";
    let confirmed = null;
    const drafts = new Map();
    let editTimer = null;
    let busy = false;
    let resetRequested = false;
    let saveRequested = false;
    let saved = null; // Only a SAVE success in this connection establishes this.
    let changedSinceSync = false;
    let recordingKey = null;
    const shortcutChoices = new Set(); // UI-only selection before a key is recorded.
    const recorder = createShortcutRecorder(document, {
      preview(text, error) {
        byId("recorder-preview").textContent = text;
        byId("recorder-preview").dataset.error = String(error);
      },
      commit(value) {
        const key = recordingKey;
        stopRecording();
        if (key) proposeAction(key, value);
      },
      cancel() {
        stopRecording();
        message("記録をキャンセルしました。割当は変更していません。");
        render();
      }
    });
    const hasActions = () => confirmed && ACTION_KEYS.every(key => validAction(confirmed[key]));
    function stopRecording() {
      recorder.stop();
      recordingKey = null;
      byId("recorder").hidden = true;
    }
    function proposeAction(key, value) {
      if (connectionState !== "connected" || !hasActions() || resetRequested || saveRequested || !validAction(value)) return;
      drafts.set(key, { value, ready: true });
      render();
      void pump();
    }

    function message(text, kind = "info") {
      byId("message").textContent = text;
      byId("message").dataset.kind = kind;
      // Routine confirmations are already represented by the controls/status.
      byId("message").hidden = kind === "info" &&
        (text.startsWith("設定を取得しました。") || text.startsWith("デバイスの応答を確認しました。"));
    }
    function format(value) {
      return typeof value === "boolean" ? (value ? "On" : "Off") :
        `${Number(value.toFixed(6)).toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 6 })}×`;
    }
    function render() {
      const ready = connectionState === "connected" && confirmed !== null;
      byId("connect").disabled = !(supported || httpMode) || connectionState !== "disconnected";
      byId("disconnect").disabled = !session || connectionState === "disconnecting";
      byId("pointer-controls").disabled = !ready || resetRequested || saveRequested || Boolean(recordingKey);
      byId("wheel-controls").disabled = byId("pointer-controls").disabled;
      byId("button-controls").disabled = !ready || !hasActions() || resetRequested || saveRequested || Boolean(recordingKey);
      byId("buttons-support").textContent = !ready ? "接続後に割当を取得します。" : hasActions() ?
        "操作はRAMへ反映します。再起動後も使うにはSaveしてください。" : "Firmware update required for button mapping";
      byId("reset").disabled = !ready || resetRequested || saveRequested || Boolean(recordingKey);
      byId("save").disabled = !ready || resetRequested || saveRequested || Boolean(recordingKey);
      let saveState = "保存状態未確認";
      if (!ready) saveState = "—";
      else if (saveRequested) saveState = "Saving…";
      else if (drafts.size || resetRequested) saveState = "Unsaved changes";
      else if (saved) saveState = ALL_KEYS.every(key => saved[key] === confirmed[key]) ? "Saved" : "Unsaved changes";
      else if (changedSinceSync) saveState = "Unsaved changes";
      byId("save-status").textContent = saveState;
      byId("save-status").dataset.saved = String(saveState === "Saved");
      const labels = { discovering: "Checking authorized devices…", disconnected: "Disconnected", connecting: "Connecting…", syncing: "Connected · Syncing…", connected: "Connected", disconnecting: "Disconnecting…" };
      byId("connection-status").textContent = labels[connectionState];
      byId("connection-status").dataset.state = connectionState;
      for (const key of KEYS) {
        const draft = drafts.get(key);
        const value = draft ? draft.value : confirmed?.[key];
        if (controls[key].type === "checkbox") controls[key].checked = value === true;
        else {
          if (value !== undefined) controls[key].value = isWheelSensitivity(key) ? wheelSliderFromSensitivity(value) : value;
          if (isWheelSensitivity(key)) controls[key].setAttribute("aria-valuetext", value === undefined ? "—" : format(value));
          byId(`${key}-value`).textContent = value === undefined ? "—" : format(value);
        }
        byId(`${key}-confirmed`).textContent = `デバイス確認値: ${confirmed ? format(confirmed[key]) : "—"}${draft ? " · 反映待ち" : ""}`;
      }
      for (const key of ACTION_KEYS) {
        const draft = drafts.get(key);
        const value = draft ? draft.value : confirmed?.[key];
        const keyboard = shortcutChoices.has(key) || (typeof value === "string" && value.startsWith("key:"));
        byId(key).value = keyboard ? "shortcut" : value || "disabled";
        byId(`${key}-shortcut`).hidden = !keyboard;
        byId(`${key}-shortcut-value`).textContent = typeof value === "string" && value.startsWith("key:") ? actionLabel(value) : "Not recorded";
        byId(`${key}-confirmed`).textContent = `デバイス確認値: ${actionLabel(confirmed?.[key])}`;
        byId(`${key}-pending`).textContent = draft ? `反映待ち: ${actionLabel(draft.value)}` : "";
      }
    }
    function discardDrafts() {
      shortcutChoices.clear();
      stopRecording();
      clearTimeout(editTimer);
      editTimer = null;
      drafts.clear();
      resetRequested = false;
      saveRequested = false;
    }
    async function disconnect(reason = "切断しました。再接続できます。", kind = "info") {
      if (connectionState === "disconnecting") return;
      const previousAttempt = attempt;
      if (previousAttempt) previousAttempt.cancelled = true;
      const previous = session;
      session = null; // Ignore responses/tasks belonging to the old connection.
      discardDrafts();
      confirmed = null;
      saved = null;
      changedSinceSync = false;
      busy = false;
      connectionState = "disconnecting";
      previous?.heartbeat?.stop();
      previous?.protocol.close();
      render();
      try {
        await previous?.close();
      } catch (error) {
        reason += ` ポート解放エラー: ${error.message}。再接続できない場合はUSBを挿し直してください。`;
        kind = "error";
      }
      if (attempt === previousAttempt) attempt = null;
      connectionState = "disconnected";
      byId("port-info").textContent = "USB Serial · 115200 baud";
      message(reason, kind);
      render();
    }
    async function recover(active) {
      discardDrafts();
      confirmed = null;
      saved = null;
      changedSinceSync = true;
      connectionState = "syncing";
      message("応答がtimeoutしました。変更の適用結果をGETで再確認しています。", "error");
      render();
      try {
        const config = await active.protocol.request("GET", { resync: true });
        if (session !== active) return;
        confirmed = config;
        connectionState = "connected";
        active.heartbeat?.start();
        message("デバイスの現在値を再取得しました。未送信の変更は破棄しました。保存結果は未確認です。必要ならSaveを再実行してください。");
      } catch (error) {
        if (session === active) await disconnect(`再同期できませんでした: ${error.message}。再接続してください。`, "error");
      }
    }
    async function pump() {
      if (busy || !session || session.heartbeat?.isBusy() || connectionState !== "connected") return;
      const active = session;
      const entry = [...drafts.entries()].find(([, draft]) => draft.ready);
      if (!resetRequested && !entry && !saveRequested) return;
      busy = true;
      const resetting = resetRequested;
      const saving = saveRequested && !resetting && !entry;
      const [key, draft] = entry || [];
      const command = resetting ? "RESET" : saving ? "SAVE" : `SET ${key} ${typeof draft.value === "string" ? draft.value : typeof draft.value === "boolean" ? Number(draft.value) : draft.value.toFixed(isWheelSensitivity(key) ? 6 : 2)}`;
      message(resetting ? "default値へ戻しています…" : saving ? "Flashへ保存しています…" : "デバイスへ反映しています…");
      try {
        const config = await active.protocol.request(command);
        if (session !== active) return;
        confirmed = config;
        if (saving) {
          saved = { ...config };
          changedSinceSync = false;
          saveRequested = false;
        } else if (resetting) resetRequested = false;
        else if (drafts.get(key) === draft) drafts.delete(key);
        if (!saving) changedSinceSync = true;
        message(saving ? "保存成功をデバイスで確認しました。再起動後もこの設定を読み込みます。" :
          "デバイスの応答を確認しました。RAMに反映済みです。永続化するにはSaveを押してください。");
      } catch (error) {
        if (session !== active) return;
        if (saving) {
          saved = null;
          changedSinceSync = true;
        }
        if (error.code === "TIMEOUT") await recover(active);
        else if (error.code?.startsWith("WRITE_FAILED")) await disconnect(`送信エラー: ${error.message}`, "error");
        else {
          // A rejected SET leaves the device unchanged. Discard local proposals.
          discardDrafts();
          message(`デバイスが設定を受け付けませんでした: ${error.message}`, "error");
        }
      } finally {
        if (session === active) {
          busy = false;
          render();
          void pump();
        }
      }
    }
    function beginAttempt(state) {
      if (!(supported || httpMode) || attempt || session || connectionState !== "disconnected") return null;
      attempt = { cancelled: false };
      connectionState = state;
      render();
      return attempt;
    }
    const currentAttempt = owner => attempt === owner && !owner.cancelled;
    function newConnection(port, timeoutMs = RESPONSE_TIMEOUT_MS) {
      const active = createDeviceConnection(port, error => {
        if (session !== active) return;
        if (!attempt && connectionState === "connected") {
          void disconnect(`接続が失われました: ${error.message}`, "error");
        }
      }, timeoutMs);
      session = active;
      return active;
    }
    async function connectSelectedPort(port, owner) {
      const active = httpMode ? createHttpConnection(fetchRequest) : newConnection(port);
      session = active;
      connectionState = "syncing";
      message("RedPointをGETで確認しています…");
      render();
      try {
        const config = await active.openAndSync();
        if (!currentAttempt(owner) || session !== active) return;
        adoptConnection(active, config);
      } catch (error) {
        try { await active.close(); }
        catch (closeError) { throw new Error(`ポート解放エラー: ${closeError.message}。USBを挿し直してください。`); }
        finally { if (session === active) session = null; }
        throw error;
      }
    }
    function adoptConnection(active, config) {
      confirmed = config;
      const info = active.port?.getInfo() || {};
      byId("port-info").textContent = httpMode ? "USB Ethernet · IPv4 Link-Local" : info.usbVendorId === undefined ? "USB Serial · 115200 baud" :
        `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")} · 115200 baud`;
      active.heartbeat = createHeartbeat(active.protocol,
        () => session === active && connectionState === "connected" && !busy &&
          !drafts.size && !resetRequested && !saveRequested && !recordingKey,
        async error => {
          if (session !== active) return;
          if (error.code === "TIMEOUT") {
            await recover(active);
            if (session === active) render();
          } else await disconnect(`Heartbeat通信エラー: ${error.message}`, "error");
        }, () => { if (session === active) void pump(); });
      attempt = null;
      connectionState = "connected";
      active.heartbeat.start();
      message("設定を取得しました。操作した項目はデバイスの応答後に確定します。");
      render();
    }
    async function chooseTransport() {
      if (fetchRequest) {
        const owner = { cancelled: false };
        attempt = owner;
        connectionState = "discovering";
        message("USB EthernetのRedPointを確認しています…");
        render();
        const active = createHttpConnection(fetchRequest);
        session = active;
        try {
          const config = await active.openAndSync();
          if (!currentAttempt(owner) || session !== active) return;
          httpMode = true;
          adoptConnection(active, config);
          return;
        } catch {
          await active.close();
          if (!currentAttempt(owner)) return;
          session = null;
          attempt = null;
          connectionState = "disconnected";
        }
      }
      if (!supported) message(secureContext ?
        "このブラウザはWeb Serialに対応していません。USB Ethernet接続またはデスクトップ版Chrome/Edgeを使用してください。" :
        "USB Ethernet APIを確認できませんでした。Web SerialにはlocalhostまたはHTTPSが必要です。", "error");
      else message("ConnectからRedPointへのアクセスを許可してください。");
      render();
      if (supported) void autoConnectAuthorizedPorts();
    }
    function finishAttempt(owner, text, kind = "info") {
      if (!currentAttempt(owner)) return;
      attempt = null;
      session = null;
      connectionState = "disconnected";
      confirmed = null;
      message(text, kind);
      render();
    }
    async function connectManual() {
      const owner = beginAttempt("connecting");
      if (!owner) return;
      message(httpMode ? "USB Ethernetへ再接続しています…" : "RedPointのSerialポートを選んでください。");
      try {
        // Keep the picker unfiltered so devices without VID metadata still work.
        // Only this click handler may request new browser permission.
        const port = httpMode ? null : await serial.requestPort();
        if (currentAttempt(owner)) await connectSelectedPort(port, owner);
      } catch (error) {
        finishAttempt(owner, error.name === "NotFoundError" ? "ポート選択をキャンセルしました。" :
          `RedPointとして接続できませんでした: ${error.message}。Serial Monitorを閉じ、Connectから再試行してください。`,
        error.name === "NotFoundError" ? "info" : "error");
      }
    }
    async function autoConnectAuthorizedPorts() {
      if (httpMode || autoConnectSuppressed || !serial?.getPorts) return;
      const owner = beginAttempt("discovering");
      if (!owner) return;
      message("許可済みのRedPointを確認しています…");
      try {
        const ports = authorizedCandidates(await serial.getPorts());
        if (!currentAttempt(owner)) return;
        if (!ports.length) {
          finishAttempt(owner, "ConnectからRedPointへのアクセスを許可してください。");
          return;
        }
        let selected = ports[0];
        if (ports.length > 1) {
          const matches = await probeAuthorizedPorts(ports,
            port => newConnection(port, AUTO_PROBE_TIMEOUT_MS), () => currentAttempt(owner));
          if (!currentAttempt(owner)) return;
          session = null;
          if (matches.length !== 1) {
            finishAttempt(owner, matches.length ?
              "複数のRedPointが見つかりました。Connectから使用するデバイスを選んでください。" :
              "許可済みportをRedPointとして確認できませんでした。Connectから再試行してください。");
            return;
          }
          selected = matches[0];
        }
        if (currentAttempt(owner)) await connectSelectedPort(selected, owner);
      } catch (error) {
        finishAttempt(owner, `自動接続できませんでした: ${error.message}。Connectから再試行してください。`);
      }
    }
    for (const key of KEYS) {
      controls[key].addEventListener("input", () => {
        if (connectionState !== "connected" || resetRequested || saveRequested || recordingKey) return;
        const value = controls[key].type === "checkbox" ? controls[key].checked : (isWheelSensitivity(key) ? wheelSensitivityFromSlider(Number(controls[key].value)) : Number(controls[key].value));
        if (typeof value === "number" && (!Number.isFinite(value) || value < 0 || value > 10)) return;
        drafts.set(key, { value, ready: false });
        clearTimeout(editTimer);
        editTimer = setTimeout(() => {
          for (const draft of drafts.values()) draft.ready = true;
          void pump();
        }, EDIT_DEBOUNCE_MS);
        render();
      });
    }
    for (const key of ACTION_KEYS) {
      byId(key).addEventListener("change", () => {
        if (recordingKey || connectionState !== "connected" || !hasActions() || resetRequested || saveRequested) return;
        const value = byId(key).value;
        if (value === "shortcut") { shortcutChoices.add(key); render(); }
        else { shortcutChoices.delete(key); proposeAction(key, value); }
      });
      byId(`${key}-record`).addEventListener("click", () => {
        if (connectionState !== "connected" || !hasActions() || resetRequested || saveRequested || recordingKey) return;
        recordingKey = key;
        byId("recorder").hidden = false;
        byId("recorder-title").textContent = `Recording shortcut… (${key.replace("Action", "")})`;
        recorder.start();
        render();
      });
    }
    byId("recorder-cancel").addEventListener("click", () => recorder.cancel());
    byId("connect").addEventListener("click", () => { void connectManual(); });
    byId("disconnect").addEventListener("click", () => {
      autoConnectSuppressed = true;
      void disconnect();
    });
    byId("reset").addEventListener("click", () => {
      if (connectionState !== "connected" || resetRequested || saveRequested || recordingKey) return;
      discardDrafts();
      resetRequested = true; // Wait for an in-flight SET before RESET.
      render();
      void pump();
    });
    byId("save").addEventListener("click", () => {
      if (connectionState !== "connected" || resetRequested || saveRequested || recordingKey) return;
      clearTimeout(editTimer);
      // Flush the latest local proposals before SAVE; never save stale RAM values.
      for (const draft of drafts.values()) draft.ready = true;
      saveRequested = true;
      render();
      void pump();
    });
    serial?.addEventListener("disconnect", event => {
      if (session?.port && (event.port || event.target) === session.port) {
        session.fail(new Error("USBデバイスが切断されました。"));
      }
    });
    serial?.addEventListener("connect", () => { void autoConnectAuthorizedPorts(); });
    render();
    void chooseTransport();
  }

  // Node's built-in test runner can exercise the actual code without a build tool.
  if (typeof module !== "undefined" && module.exports) {
    module.exports = { wheelSensitivityFromSlider, wheelSliderFromSensitivity, validConfig, parseLine, createLineReader, createProtocol, createSerialTransport, createHeartbeat, createHttpTransport, createHttpConnection, authorizedCandidates, createDeviceConnection, probeAuthorizedPorts, mount };
  } else mount(document, navigator.serial, window.isSecureContext);
})();
