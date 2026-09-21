"use strict";

(() => {
  const KEYS = ["pointerSensitivity", "middleSensitivity", "invertX", "invertY"];
  const RESPONSE_TIMEOUT_MS = 2000;
  const EDIT_DEBOUNCE_MS = 120;
  const MAX_RESPONSE_LINE = 2048;

  // Protocol: independent of the browser, SerialPort and DOM.
  function validConfig(config) {
    return config && typeof config === "object" &&
      ["pointerSensitivity", "middleSensitivity"].every(key =>
        typeof config[key] === "number" && Number.isFinite(config[key]) &&
        config[key] >= 0 && config[key] <= 10) &&
      typeof config.invertX === "boolean" && typeof config.invertY === "boolean";
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
          ["GET", "SET", "RESET"].includes(response.command) && validConfig(response.config)) {
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
      // During resync, a late SET/RESET/error must not complete GET.
      if (!response.ok) {
        if (!pending.resync) finish(protocolError(response.error));
      } else if (response.command === pending.command) {
        needsSync = false;
        finish(null, Object.fromEntries(KEYS.map(key => [key, response.config[key]])));
      }
    });
    return {
      accept,
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

  // UI: confirmed configuration comes only from a matched device response.
  function mount(document, serial, secureContext) {
    const byId = id => document.getElementById(id);
    const controls = Object.fromEntries(KEYS.map(key => [key, byId(key)]));
    const supported = Boolean(serial && secureContext);
    let session = null;
    let connectionState = "disconnected";
    let confirmed = null;
    const drafts = new Map();
    let editTimer = null;
    let busy = false;
    let resetRequested = false;

    function message(text, kind = "info") {
      byId("message").textContent = text;
      byId("message").dataset.kind = kind;
    }
    function format(value) {
      return typeof value === "boolean" ? (value ? "On" : "Off") :
        `${Number(value.toFixed(6)).toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 6 })}×`;
    }
    function render() {
      const ready = connectionState === "connected" && confirmed !== null;
      byId("connect").disabled = !supported || connectionState !== "disconnected";
      byId("disconnect").disabled = !session || connectionState === "disconnecting";
      byId("pointer-controls").disabled = !ready || resetRequested;
      byId("reset").disabled = !ready || resetRequested;
      const labels = { disconnected: "Disconnected", connecting: "Connecting…", syncing: "Connected · Syncing…", connected: "Connected", disconnecting: "Disconnecting…" };
      byId("connection-status").textContent = labels[connectionState];
      byId("connection-status").dataset.state = connectionState;
      for (const key of KEYS) {
        const draft = drafts.get(key);
        const value = draft ? draft.value : confirmed?.[key];
        if (controls[key].type === "checkbox") controls[key].checked = value === true;
        else {
          if (value !== undefined) controls[key].value = value;
          byId(`${key}-value`).textContent = value === undefined ? "—" : format(value);
        }
        byId(`${key}-confirmed`).textContent = `デバイス確認値: ${confirmed ? format(confirmed[key]) : "—"}${draft ? " · 反映待ち" : ""}`;
      }
    }
    function discardDrafts() {
      clearTimeout(editTimer);
      editTimer = null;
      drafts.clear();
      resetRequested = false;
    }
    async function disconnect(reason = "切断しました。再接続できます。", kind = "info") {
      const previous = session;
      session = null; // Ignore responses/tasks belonging to the old connection.
      discardDrafts();
      confirmed = null;
      busy = false;
      connectionState = "disconnecting";
      previous?.protocol.close();
      render();
      try {
        await previous?.transport.close();
      } catch (error) {
        reason += ` ポート解放エラー: ${error.message}。再接続できない場合はUSBを挿し直してください。`;
        kind = "error";
      }
      connectionState = "disconnected";
      byId("port-info").textContent = "USB Serial · 115200 baud";
      message(reason, kind);
      render();
    }
    async function recover(active) {
      discardDrafts();
      confirmed = null;
      connectionState = "syncing";
      message("応答がtimeoutしました。変更の適用結果をGETで再確認しています。", "error");
      render();
      try {
        const config = await active.protocol.request("GET", { resync: true });
        if (session !== active) return;
        confirmed = config;
        connectionState = "connected";
        message("デバイスの現在値を再取得しました。未送信の変更は破棄しました。");
      } catch (error) {
        if (session === active) await disconnect(`再同期できませんでした: ${error.message}。再接続してください。`, "error");
      }
    }
    async function pump() {
      if (busy || !session || connectionState !== "connected") return;
      const active = session;
      const entry = [...drafts.entries()].find(([, draft]) => draft.ready);
      if (!resetRequested && !entry) return;
      busy = true;
      const resetting = resetRequested;
      const [key, draft] = entry || [];
      const command = resetting ? "RESET" : `SET ${key} ${typeof draft.value === "boolean" ? Number(draft.value) : draft.value.toFixed(2)}`;
      message(resetting ? "default値へ戻しています…" : "デバイスへ反映しています…");
      try {
        const config = await active.protocol.request(command);
        if (session !== active) return;
        confirmed = config;
        if (resetting) resetRequested = false;
        else if (drafts.get(key) === draft) drafts.delete(key);
        message("デバイスの応答を確認しました。設定はRAMに反映されています。");
      } catch (error) {
        if (session !== active) return;
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
    async function connect() {
      if (connectionState !== "disconnected" || !supported) return;
      connectionState = "connecting";
      message("RedPointのSerialポートを選んでください。");
      render();
      let port;
      let opened = false;
      let active;
      try {
        port = await serial.requestPort();
        await port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
        opened = true;
        active = {};
        active.port = port;
        active.transport = createSerialTransport(port, text => active.protocol.accept(text), error => {
          if (session === active) void disconnect(`接続が失われました: ${error.message}`, "error");
        });
        active.protocol = createProtocol(text => active.transport.send(text));
        session = active;
        connectionState = "syncing";
        const info = port.getInfo();
        byId("port-info").textContent = info.usbVendorId === undefined ? "USB Serial · 115200 baud" :
          `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")} · 115200 baud`;
        active.transport.start();
        message("接続しました。デバイスの設定を取得しています…");
        render();
        // Leading newline also terminates any incomplete command from an earlier session.
        const config = await active.protocol.request("GET", { resync: true });
        if (session !== active) return;
        confirmed = config;
        connectionState = "connected";
        message("設定を取得しました。操作した項目はデバイスの応答後に確定します。");
      } catch (error) {
        if (active && session !== active) return;
        if (active && error.code === "TIMEOUT") await recover(active);
        else if (active) await disconnect(`接続エラー: ${error.message}`, "error");
        else {
          if (opened) await port.close().catch(() => {});
          connectionState = "disconnected";
          message(error.name === "NotFoundError" ? "ポート選択をキャンセルしました。" :
            `接続できませんでした: ${error.message}。Serial Monitorなどでポートを使用していないか確認してください。`,
          error.name === "NotFoundError" ? "info" : "error");
        }
      }
      render();
    }
    for (const key of KEYS) {
      controls[key].addEventListener("input", () => {
        if (connectionState !== "connected" || resetRequested) return;
        const value = controls[key].type === "checkbox" ? controls[key].checked : Number(controls[key].value);
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
    byId("connect").addEventListener("click", () => { void connect(); });
    byId("disconnect").addEventListener("click", () => { void disconnect(); });
    byId("reset").addEventListener("click", () => {
      if (connectionState !== "connected" || resetRequested) return;
      discardDrafts();
      resetRequested = true; // Wait for an in-flight SET before RESET.
      render();
      void pump();
    });
    serial?.addEventListener("disconnect", event => {
      if (session && event.target === session.port) void disconnect("USBデバイスが切断されました。再接続できます。", "error");
    });
    if (!supported) message(secureContext ?
      "このブラウザはWeb Serialに対応していません。デスクトップ版ChromeまたはEdgeで開いてください。" :
      "Web Serialには安全な接続が必要です。localhostまたはHTTPSで開いてください。", "error");
    render();
  }

  // Node's built-in test runner can exercise the actual code without a build tool.
  if (typeof module !== "undefined" && module.exports) {
    module.exports = { validConfig, parseLine, createLineReader, createProtocol, createSerialTransport, mount };
  } else mount(document, navigator.serial, window.isSecureContext);
})();
