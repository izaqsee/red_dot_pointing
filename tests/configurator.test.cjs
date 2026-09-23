const { test } = require("node:test");
const assert = require("node:assert/strict");
const { validConfig, parseLine, createLineReader, createProtocol, createSerialTransport, createHeartbeat, createHttpTransport, createHttpConnection, authorizedCandidates, createDeviceConnection, probeAuthorizedPorts, mount } = require("../configurator/app.js");

const defaults = () => ({ pointerSensitivity: 1, wheelSensitivityX: 0.4, wheelSensitivityY: 0.4, wheelInvertX: false, wheelInvertY: false, pointerInvertX: false, pointerInvertY: false });
const actionDefaults = () => ({ ...defaults(), leftAction: "mouse:left", middleAction: "mouse:middle", rightAction: "mouse:right" });
const { CODE_MAP, validAction, actionLabel, createShortcutRecorder } = require("../configurator/shortcuts.js");
const response = (command = "GET", config = defaults()) => `@CONFIG ${JSON.stringify({ ok: true, command, config })}\r\n`;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(condition, timeout = 1500) {
  const start = Date.now();
  while (!condition()) {
    if (Date.now() - start > timeout) throw new Error("Condition timed out");
    await sleep(5);
  }
}

test("line reader reconstructs chunks/CRLF and bounds malformed input", () => {
  const lines = [];
  const accept = createLineReader(line => lines.push(line));
  accept("@DEB"); accept("UG PTR 1\r"); accept("\n" + response().slice(0, 25));
  accept(response().slice(25));
  accept("x".repeat(3000)); accept("\n" + response("RESET"));
  assert.deepEqual(lines.map(line => parseLine(line).kind), ["debug", "config", "config"]);
  assert.equal(parseLine("unknown").kind, "unknown");
  for (const line of ["@CONFIG {", "@CONFIG null", '@CONFIG {"ok":true}', response("GET", { ...defaults(), pointerInvertX: 1 }).trim(), response("GET", { ...defaults(), pointerSensitivity: 11 }).trim()]) {
    assert.equal(parseLine(line).kind, "invalid");
  }
});

test("single request, mixed debug/invalid input, authoritative response and device error", async () => {
  const writes = [];
  const protocol = createProtocol(async line => writes.push(line), 100);
  const result = protocol.request("SET wheelSensitivityX 0.25");
  await assert.rejects(protocol.request("GET"), /BUSY/);
  protocol.accept('@DEBUG PTR anything\n@CONFIG invalid\nunknown\n');
  protocol.accept(response("GET")); // A different command cannot complete SET.
  protocol.accept(response("SET", { ...defaults(), wheelSensitivityX: 0.25 }));
  assert.equal((await result).wheelSensitivityX, 0.25);
  assert.deepEqual(writes, ["SET wheelSensitivityX 0.25\n"]);
  const rejected = protocol.request("SET pointerInvertX 1");
  protocol.accept('@CONFIG {"ok":false,"error":"INVALID_VALUE"}\n');
  await assert.rejects(rejected, /INVALID_VALUE/);
  protocol.close();
});

test("timeout blocks mutation until GET resync; late SET/errors do not resolve GET", async () => {
  const writes = [];
  const protocol = createProtocol(async line => writes.push(line), 25);
  await assert.rejects(protocol.request("SET pointerInvertX 1"), /TIMEOUT/);
  await assert.rejects(protocol.request("SET pointerInvertY 1"), /RESYNC_REQUIRED/);
  let settled = false;
  const get = protocol.request("GET", { resync: true }).then(config => { settled = true; return config; });
  protocol.accept(response("SET"));
  protocol.accept('@CONFIG {"ok":false,"error":"INVALID_LINE"}\n');
  await sleep(2);
  assert.equal(settled, false);
  protocol.accept(response("GET", { ...defaults(), pointerInvertX: true }));
  assert.equal((await get).pointerInvertX, true);
  assert.equal(writes[1], "\nGET\n");
  const pending = protocol.request("GET");
  protocol.close();
  await assert.rejects(pending, /DISCONNECTED/);
});

test("write errors and stalled writes are bounded", async () => {
  const failing = createProtocol(async () => { throw new Error("unplugged"); });
  await assert.rejects(failing.request("GET"), /WRITE_FAILED/);
  failing.close();
  const stalled = createProtocol(() => new Promise(() => {}), 15);
  await assert.rejects(stalled.request("GET"), /TIMEOUT/);
  stalled.close();
});

function fakePort(savedConfig = null, onSave = () => {}, withActions = false) {
  let controller;
  const port = {
    config: savedConfig ? { ...savedConfig } : withActions ? actionDefaults() : defaults(), commands: [], allCommands: [], pingSupported: false, writes: [], held: [], hold: false, rejectNext: false,
    opened: false, closed: false, openCalls: 0, closeCalls: 0,
    readable: new ReadableStream({ start(c) { controller = c; } }),
    async open(options) {
      if (port.openFailure) throw new Error("port busy");
      if (port.closed) {
        port.readable = new ReadableStream({ start(c) { controller = c; } });
        port.writable = createWritable();
      }
      port.openCalls++; port.opened = true; port.closed = false; port.options = options;
    },
    async close() {
      assert.equal(port.readable.locked, false);
      assert.equal(port.writable.locked, false);
      port.closeCalls++;
      if (port.closeFailure) throw new Error("close failed");
      port.closed = true;
    },
    getInfo() { return { usbVendorId: 0x2e8a, usbProductId: 10 }; },
    emit(text) { controller.enqueue(new TextEncoder().encode(text)); },
    release() { for (const reply of port.held.splice(0)) port.emit(reply); },
    unplug() { controller.error(new Error("USB removed")); }
  };
  const createWritable = () => new WritableStream({
    write(bytes) {
      const text = new TextDecoder().decode(bytes);
      port.writes.push(text);
      for (const line of text.split("\n").filter(Boolean)) {
        port.allCommands.push(line);
        // Existing UI regressions assert only user configuration commands.
        if (line === "PING") {
          if (port.holdPing) continue;
          port.emit(port.pingSupported ? '@CONFIG {"ok":true,"command":"PING"}\n' :
            '@CONFIG {"ok":false,"error":"UNKNOWN_COMMAND"}\n');
          continue;
        }
        port.commands.push(line);
        const [command, key, value] = line.split(" ");
        if (command === "GET" && port.getReply !== undefined) {
          const replies = typeof port.getReply === "function" ? port.getReply() : port.getReply;
          for (const text of Array.isArray(replies) ? replies : [replies]) if (text) port.emit(text);
          continue;
        }
        let reply;
        if (port.rejectNext) {
          port.rejectNext = false;
          reply = '@CONFIG {"ok":false,"error":"INVALID_VALUE"}\n';
        } else {
          if (command === "SET") port.config[key] = key.endsWith("Action") ? value : key.includes("Invert") ? value === "1" : Number(value);
          if (command === "RESET") port.config = withActions ? actionDefaults() : defaults();
          if (command === "SAVE") onSave({ ...port.config });
          reply = response(command, port.config);
        }
        if (port.hold) port.held.push(reply);
        else port.emit(reply);
      }
    }
  });
  port.writable = createWritable();
  return port;
}

function eventTarget() {
  const events = new Map();
  return {
    addEventListener(type, handler) { if (!events.has(type)) events.set(type, new Set()); events.get(type).add(handler); },
    removeEventListener(type, handler) { events.get(type)?.delete(handler); },
    fire(type, event = {}) { for (const handler of [...(events.get(type) || [])]) handler(event); },
    listeners(type) { return events.get(type)?.size || 0; }
  };
}

function keyEvent(code, extras = {}) {
  return {
    code, key: "deliberately wrong", ctrlKey: false, shiftKey: false, altKey: false, metaKey: false,
    repeat: false, isComposing: false, prevented: false, stopped: false,
    preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; },
    ...extras
  };
}

function fakeDocument() {
  const elements = new Map();
  const doc = { ...eventTarget(), defaultView: eventTarget(), getElementById(id) {
    if (!elements.has(id)) elements.set(id, {
      type: id.includes("Invert") ? "checkbox" : "range", value: "", checked: false,
      disabled: false, dataset: {}, textContent: "", events: {},
      addEventListener(type, handler) { this.events[type] = handler; },
      fire(type) { this.events[type]?.({ target: this }); }
    });
    return elements.get(id);
  } };
  return doc;
}

function setupUI(withActions = false, pingSupported = false, configurePort = () => {}) {
  const doc = fakeDocument();
  const ports = [];
  let persisted = null;
  const serial = {
    async requestPort() {
      const port = fakePort(persisted, config => { persisted = config; }, withActions);
      port.pingSupported = pingSupported;
      configurePort(port);
      ports.push(port);
      return port;
    },
    addEventListener() {}
  };
  mount(doc, serial, true);
  const el = id => doc.getElementById(id);
  return { doc, ports, serial, el, async connect() {
    el("connect").fire("click");
    await until(() => el("connection-status").textContent === "Connected");
    return ports.at(-1);
  }, async disconnect() {
    el("disconnect").fire("click");
    await until(() => el("connection-status").textContent === "Disconnected");
  } };
}

test("transport closes locks and reports stream loss", async () => {
  const port = fakePort();
  let lost = false;
  let received = "";
  const transport = createSerialTransport(port, text => received += text, () => { lost = true; });
  transport.start();
  await transport.send("GET\n");
  await until(() => received.includes("@CONFIG"));
  port.unplug();
  await until(() => lost);
  await transport.close();
  assert.equal(port.closed, true);
});

test("UI confirms only replies, coalesces edits, preserves newer drafts, queues reset", async () => {
  const ui = setupUI();
  assert.equal(ui.el("pointer-controls").disabled, true);
  const port = await ui.connect();
  assert.deepEqual(port.commands, ["GET"]);
  assert.equal(port.options.baudRate, 115200);
  assert.equal(ui.el("pointer-controls").disabled, false);
  port.hold = true;
  for (const value of ["1.1", "1.2", "1.3"]) {
    ui.el("pointerSensitivity").value = value;
    ui.el("pointerSensitivity").fire("input");
  }
  assert.equal(ui.el("pointerSensitivity-value").textContent, "1.30×");
  assert.match(ui.el("pointerSensitivity-confirmed").textContent, /1.00×.*反映待ち/);
  await until(() => port.commands.length === 2);
  assert.equal(port.commands[1], "SET pointerSensitivity 1.30");
  ui.el("pointerSensitivity").value = "1.4";
  ui.el("pointerSensitivity").fire("input");
  port.release();
  await until(() => ui.el("pointerSensitivity-confirmed").textContent.includes("1.30×"));
  assert.equal(ui.el("pointerSensitivity-value").textContent, "1.40×");
  await until(() => port.commands.length === 3);
  ui.el("pointerInvertX").checked = true;
  ui.el("pointerInvertX").fire("input");
  ui.el("reset").fire("click");
  assert.equal(ui.el("pointer-controls").disabled, true);
  port.hold = false;
  port.release();
  await until(() => port.commands.includes("RESET"));
  await until(() => !ui.el("pointer-controls").disabled);
  assert.deepEqual(port.commands, ["GET", "SET pointerSensitivity 1.30", "SET pointerSensitivity 1.40", "RESET"]);
  assert.equal(ui.el("pointerSensitivity-value").textContent, "1.00×");
  await ui.disconnect();
  assert.equal(port.closed, true);
  assert.equal(ui.el("pointer-controls").disabled, true);
});

test("rejected edits restore confirmed state; unplug and reconnect work", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.rejectNext = true;
  ui.el("pointerInvertY").checked = true;
  ui.el("pointerInvertY").fire("input");
  await until(() => ui.el("message").textContent.includes("INVALID_VALUE"));
  assert.equal(ui.el("pointerInvertY").checked, false);
  port.unplug();
  await until(() => ui.el("connection-status").textContent === "Disconnected");
  assert.equal(ui.el("connect").disabled, false);
  const second = await ui.connect();
  assert.notEqual(second, port);
  assert.deepEqual(second.commands, ["GET"]);
  await ui.disconnect();
});

test("unsupported browser, insecure context and selection cancellation recover", async () => {
  for (const [serial, secure] of [[undefined, true], [{ addEventListener() {} }, false]]) {
    const doc = fakeDocument(); mount(doc, serial, secure);
    assert.equal(doc.getElementById("connect").disabled, true);
    assert.equal(doc.getElementById("pointer-controls").disabled, true);
  }
  const ui = setupUI();
  ui.serial.requestPort = async () => { const error = new Error(); error.name = "NotFoundError"; throw error; };
  ui.el("connect").fire("click");
  await until(() => ui.el("connection-status").textContent === "Disconnected");
  assert.match(ui.el("message").textContent, /キャンセル/);
  assert.equal(ui.el("connect").disabled, false);
});

test("UI timeout resynchronizes from device and discards unsent proposals", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("pointerInvertX").checked = true;
  ui.el("pointerInvertX").fire("input");
  await until(() => port.commands.length === 2);
  ui.el("pointerInvertY").checked = true;
  ui.el("pointerInvertY").fire("input");
  await until(() => port.commands.at(-1) === "GET", 3000);
  assert.equal(ui.el("pointer-controls").disabled, true);
  port.hold = false;
  port.release(); // Includes the late SET, then the resync GET.
  await until(() => ui.el("connection-status").textContent === "Connected");
  assert.equal(ui.el("pointerInvertX").checked, true);
  assert.equal(ui.el("pointerInvertY").checked, false);
  assert.deepEqual(port.commands, ["GET", "SET pointerInvertX 1", "GET"]);
  await ui.disconnect();
});

test("UI failed resync disconnects, then a fresh connection starts with GET", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("pointerInvertX").checked = true;
  ui.el("pointerInvertX").fire("input");
  await until(() => ui.el("connection-status").textContent === "Disconnected", 5000);
  assert.equal(port.closed, true);
  assert.equal(ui.el("connect").disabled, false);
  assert.match(ui.el("message").textContent, /再同期できません/);
  const fresh = await ui.connect();
  assert.deepEqual(fresh.commands, ["GET"]);
  assert.equal(ui.el("pointerInvertX").checked, false);
  await ui.disconnect();
});

test("disconnect cancels in-flight requests and never leaks drafts to a new session", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("pointerInvertX").checked = true;
  ui.el("pointerInvertX").fire("input");
  await until(() => port.commands.length === 2);
  await ui.disconnect();
  const fresh = await ui.connect();
  assert.equal(ui.el("pointerInvertX").checked, false);
  assert.deepEqual(fresh.commands, ["GET"]);
  await ui.disconnect();
});


test("SAVE waits for all SET replies, confirms persistence, and reconnect GET does not assume Saved", async () => {
  const ui = setupUI();
  assert.equal(ui.el("save").disabled, true);
  const port = await ui.connect();
  assert.equal(ui.el("save").disabled, false);
  assert.equal(ui.el("save-status").textContent, "保存状態未確認");
  port.hold = true;
  ui.el("wheelSensitivityX").value = "0.25";
  ui.el("wheelSensitivityX").fire("input");
  ui.el("save").fire("click"); // Before debounce expires.
  await until(() => port.commands.length === 2);
  assert.equal(port.commands[1], "SET wheelSensitivityX 0.25");
  assert.equal(ui.el("save-status").textContent, "Saving…");
  assert.equal(ui.el("pointer-controls").disabled, true);
  assert.equal(ui.el("save").disabled, true);
  port.release();
  await until(() => port.commands.at(-1) === "SAVE");
  assert.notEqual(ui.el("save-status").textContent, "Saved");
  port.release();
  await until(() => ui.el("save-status").textContent === "Saved");
  assert.equal(ui.el("save").disabled, false);
  await ui.disconnect();
  assert.equal(ui.el("save-status").textContent, "—");
  assert.equal(ui.el("save").disabled, true);
  const fresh = await ui.connect();
  assert.deepEqual(fresh.commands, ["GET"]);
  assert.equal(ui.el("wheelSensitivityX-value").textContent, "0.25×");
  assert.equal(ui.el("save-status").textContent, "保存状態未確認");
  await ui.disconnect();
});

test("SET and RESET change Saved to Unsaved without automatically sending SAVE", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  ui.el("pointerInvertX").checked = true;
  ui.el("pointerInvertX").fire("input");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  await until(() => ui.el("pointerInvertX-confirmed").textContent === "デバイス確認値: On");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  assert.equal(port.commands.filter(line => line === "SAVE").length, 1);
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  ui.el("reset").fire("click");
  await until(() => ui.el("pointerInvertX-confirmed").textContent === "デバイス確認値: Off");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  await ui.disconnect();
  await ui.connect(); // Mock reboot loads earlier saved value, not RESET's RAM default.
  assert.equal(ui.el("pointerInvertX").checked, true);
  await ui.disconnect();
});

test("SAVE failure never marks Saved and remains retryable", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("save").fire("click");
  await until(() => port.commands.at(-1) === "SAVE");
  port.held = [];
  port.emit('@CONFIG {"ok":false,"error":"SAVE_FAILED"}\n');
  await until(() => ui.el("message").textContent.includes("SAVE_FAILED"));
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  assert.equal(ui.el("save").disabled, false);
  port.hold = false;
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  await ui.disconnect();
});

test("SAVE timeout ignores late success during GET recovery and never marks Saved", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("save").fire("click");
  await until(() => port.commands.length === 3, 3000);
  assert.deepEqual(port.commands, ["GET", "SAVE", "GET"]);
  port.hold = false;
  port.release(); // Late SAVE success must not establish a saved snapshot.
  await until(() => ui.el("connection-status").textContent === "Connected");
  assert.notEqual(ui.el("save-status").textContent, "Saved");
  assert.match(ui.el("message").textContent, /保存結果は未確認/);
  assert.equal(ui.el("save").disabled, false);
  await ui.disconnect();
});

test("failed SET cancels queued SAVE; rapid repeated Save sends only once", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.rejectNext = true;
  ui.el("pointerInvertY").checked = true;
  ui.el("pointerInvertY").fire("input");
  ui.el("save").fire("click");
  await until(() => ui.el("message").textContent.includes("INVALID_VALUE"));
  assert.deepEqual(port.commands, ["GET", "SET pointerInvertY 1"]);
  assert.equal(ui.el("save").disabled, false);
  port.hold = true;
  ui.el("save").fire("click");
  ui.el("save").fire("click");
  await until(() => port.commands.at(-1) === "SAVE");
  assert.equal(port.commands.filter(line => line === "SAVE").length, 1);
  await ui.disconnect();
  assert.notEqual(ui.el("save-status").textContent, "Saved");
});


test("new action fields parse, malformed/partial actions reject, legacy config remains supported", () => {
  assert.equal(parseLine(response("GET", actionDefaults()).trim()).kind, "config");
  assert.equal(parseLine(response().trim()).kind, "config");
  for (const value of ["key:10:17", "key:00:00", "key:00:E0", "key:00:FF", "key:03:17x", "mouse:back", "key:00:66", null]) {
    assert.equal(parseLine(response("GET", { ...actionDefaults(), rightAction: value }).trim()).kind, "invalid");
  }
  assert.equal(parseLine(response("GET", { ...defaults(), leftAction: "mouse:left" }).trim()).kind, "invalid");
  assert.equal(actionLabel("key:03:17"), "Ctrl + Shift + T");
  const usages = Object.values(CODE_MAP).map(entry => entry[0]).sort((a,b) => a-b);
  const expected = Array.from({length: 0x73 - 3}, (_, i) => i + 4).filter(usage => ![0x32, 0x66].includes(usage));
  assert.deepEqual(usages, expected); // Same allowlist exercised by firmware's exhaustive API test.
  assert.equal(new Set(usages).size, usages.length);
  for (const [usage] of Object.values(CODE_MAP)) {
    assert.ok(validAction(`key:0F:${usage.toString(16).toUpperCase().padStart(2,"0")}`));
  }
});

test("recorder captures only while active, previews modifiers, ignores repeat, and records code not key", () => {
  const target = fakeDocument();
  const previews = [], commits = [];
  const recorder = createShortcutRecorder(target, { preview: text => previews.push(text), commit: value => commits.push(value), cancel() {} });
  const normal = keyEvent("KeyT"); target.fire("keydown",normal); assert.equal(normal.prevented,false);
  recorder.start();
  const ctrl = keyEvent("ControlLeft", {ctrlKey:true}); target.fire("keydown",ctrl);
  assert.ok(ctrl.prevented && ctrl.stopped); assert.equal(previews.at(-1),"Ctrl");
  target.fire("keydown",keyEvent("ShiftLeft", {ctrlKey:true,shiftKey:true}));
  assert.equal(previews.at(-1),"Ctrl + Shift");
  target.fire("keydown",keyEvent("KeyT", {ctrlKey:true,shiftKey:true,repeat:true}));
  assert.equal(commits.length,0);
  target.fire("keydown",keyEvent("KeyT", {ctrlKey:true,shiftKey:true}));
  assert.deepEqual(commits,["key:03:17"]);
  assert.equal(target.listeners("keydown"),0);assert.equal(target.listeners("keyup"),0);
  const after = keyEvent("KeyQ");target.fire("keydown",after);assert.equal(after.prevented,false);
});

test("recorder supports single keys, Escape/Backspace, arrows/keypad, releases modifiers and explicit cancel", () => {
  const target = fakeDocument(); let committed, preview, cancelled=0;
  const recorder = createShortcutRecorder(target, { preview: text => preview=text, commit: value => committed=value, cancel: () => cancelled++ });
  for (const [code, expected, extras] of [
    ["KeyA","key:00:04",{}], ["Escape","key:00:29",{}], ["Backspace","key:00:2A",{}],
    ["F5","key:00:3E",{}], ["ArrowLeft","key:04:50",{altKey:true}], ["NumpadAdd","key:00:57",{}]
  ]) {
    recorder.start();target.fire("keydown",keyEvent(code,extras));assert.equal(committed,expected);
  }
  recorder.start();target.fire("keydown",keyEvent("ControlRight",{ctrlKey:true}));
  target.fire("keyup",keyEvent("ControlRight"));assert.match(preview,/キーを押して/);
  committed=null;recorder.cancel();assert.equal(cancelled,1);assert.equal(committed,null);
  assert.equal(target.listeners("keydown"),0);
  recorder.start();target.defaultView.fire("blur", {target: target.defaultView});assert.equal(cancelled,2);
  assert.equal(target.listeners("keyup"),0);
});

test("disabling the focused Record button does not cancel recording via captured blur", () => {
  const target = fakeDocument(); let committed = null, cancelled = 0;
  const recorder = createShortcutRecorder(target, {
    preview() {}, commit: value => committed = value, cancel: () => cancelled++
  });
  recorder.start();
  target.defaultView.fire("blur", {target: target.getElementById("rightAction-record")});
  assert.equal(cancelled, 0);
  assert.equal(target.listeners("keydown"), 1);
  target.fire("keydown", keyEvent("KeyT", {ctrlKey: true, shiftKey: true}));
  assert.equal(committed, "key:03:17");
  assert.equal(target.listeners("keydown"), 0);
});

test("unsupported/composition/AltGraph leave the binding untouched and recording can continue", () => {
  const target=fakeDocument();let result=null, error=false;
  const recorder=createShortcutRecorder(target,{preview:(_,bad)=>error=bad,commit:value=>result=value,cancel() {}});
  recorder.start();
  for (const event of [keyEvent("IntlYen"),keyEvent("Unidentified"),keyEvent("KeyT",{isComposing:true}),keyEvent("KeyT",{getModifierState:()=>true})]) {
    target.fire("keydown",event);assert.equal(result,null);assert.equal(error,true);assert.equal(target.listeners("keydown"),1);
  }
  target.fire("keydown",keyEvent("KeyT"));assert.equal(result,"key:00:17");
});

test("old firmware keeps pointer controls but disables all action requests", async () => {
  const ui=setupUI();const port=await ui.connect();
  assert.equal(ui.el("button-controls").disabled,true);
  assert.match(ui.el("buttons-support").textContent,/Firmware update required/);
  ui.el("rightAction").value="disabled";ui.el("rightAction").fire("change");
  ui.el("rightAction-record").fire("click");
  assert.deepEqual(port.commands,["GET"]);assert.equal(ui.doc.listeners("keydown"),0);
  assert.equal(ui.el("pointer-controls").disabled,false);
  await ui.disconnect();
});

test("mouse/disabled selection, recorder, device confirmation, SAVE, RESET and reconnect integrate", async () => {
  const ui=setupUI(true);const port=await ui.connect();
  assert.equal(ui.el("button-controls").disabled,false);
  for (const value of ["mouse:middle","disabled","mouse:right"]) {
    ui.el("rightAction").value=value;ui.el("rightAction").fire("change");
    await until(()=>ui.el("rightAction-confirmed").textContent===`デバイス確認値: ${actionLabel(value)}`);
    assert.equal(port.commands.at(-1),`SET rightAction ${value}`);
  }
  port.hold=true;
  ui.el("rightAction-record").fire("click");
  assert.equal(ui.el("recorder").hidden,false);assert.equal(ui.el("save").disabled,true);
  ui.doc.fire("keydown",keyEvent("ControlLeft",{ctrlKey:true}));
  assert.equal(ui.el("recorder-preview").textContent,"Ctrl");
  ui.doc.fire("keydown",keyEvent("KeyT",{ctrlKey:true,shiftKey:true}));
  await until(()=>port.commands.at(-1)==="SET rightAction key:03:17");
  assert.equal(ui.el("rightAction-confirmed").textContent,"デバイス確認値: Right Click");
  assert.match(ui.el("rightAction-pending").textContent,/Ctrl \+ Shift \+ T/);
  assert.equal(ui.el("save-status").textContent,"Unsaved changes");
  assert.equal(ui.el("recorder").hidden,true);assert.equal(ui.doc.listeners("keydown"),0);
  port.hold=false;port.release();
  await until(()=>ui.el("rightAction-confirmed").textContent==="デバイス確認値: Ctrl + Shift + T");
  ui.el("save").fire("click");await until(()=>ui.el("save-status").textContent==="Saved");
  ui.el("reset").fire("click");await until(()=>ui.el("rightAction-confirmed").textContent==="デバイス確認値: Right Click");
  assert.equal(ui.el("save-status").textContent,"Unsaved changes");
  await ui.disconnect();await ui.connect();
  assert.equal(ui.el("rightAction-confirmed").textContent,"デバイス確認値: Ctrl + Shift + T");
  assert.equal(ui.el("save-status").textContent,"保存状態未確認");
  await ui.disconnect();
});

test("unsupported key/cancel/disconnect do not replace confirmed binding or leak key listeners", async () => {
  const ui=setupUI(true);const port=await ui.connect();
  ui.el("leftAction-record").fire("click");
  ui.doc.fire("keydown",keyEvent("IntlRo"));assert.match(ui.el("recorder-preview").textContent,/未対応/);
  assert.equal(ui.el("leftAction-confirmed").textContent,"デバイス確認値: Left Click");
  ui.el("recorder-cancel").fire("click");
  assert.equal(ui.el("recorder").hidden,true);assert.deepEqual(port.commands,["GET"]);
  assert.equal(ui.doc.listeners("keydown"),0);
  ui.el("leftAction-record").fire("click");await ui.disconnect();
  assert.equal(ui.el("recorder").hidden,true);assert.equal(ui.doc.listeners("keyup"),0);
});

test("action timeout resync uses returned action values and drops late SET", async () => {
  const ui=setupUI(true);const port=await ui.connect();port.hold=true;
  ui.el("middleAction").value="disabled";ui.el("middleAction").fire("change");
  await until(()=>port.commands.length===3,3000);
  assert.deepEqual(port.commands,["GET","SET middleAction disabled","GET"]);
  port.hold=false;port.release();
  await until(()=>ui.el("connection-status").textContent==="Connected");
  assert.equal(ui.el("middleAction-confirmed").textContent,"デバイス確認値: Disabled");
  assert.notEqual(ui.el("save-status").textContent,"Saved");
  await ui.disconnect();
});


test("PING response needs no config and cannot complete GET/SET", async () => {
  const p = createProtocol(async () => {});
  let done = false;
  const get = p.request("GET").then(value => { done = true; return value; });
  p.accept('@CONFIG {"ok":true,"command":"PING"}\n');
  await sleep(1); assert.equal(done, false);
  p.accept(response()); await get;
  const ping = p.request("PING");
  p.accept(response("GET")); assert.equal(p.isIdle(), false);
  p.accept('@CONFIG {"ok":true,"command":"PING"}\n');
  assert.equal(await ping, undefined); p.close();
});

test("heartbeat probes, skips busy/user drafts, resumes, and stops", async t => {
  const sent = []; let allow = false;
  const p = createProtocol(async line => { sent.push(line.trim()); if (line === "PING\n") p.accept('@CONFIG {"ok":true,"command":"PING"}\n'); });
  const h = createHeartbeat(p, () => allow, error => { throw error; }, () => {}, 15);
  t.after(() => { h.stop(); p.close(); });
  h.start(); await sleep(35); assert.deepEqual(sent, []);
  allow = true;
  const set = p.request("SET pointerInvertX 1");
  await sleep(35); assert.deepEqual(sent, ["SET pointerInvertX 1"]);
  p.accept(response("SET")); await set;
  await until(() => sent.includes("PING"));
  allow = false; const count = sent.length;
  await sleep(40); assert.equal(sent.length, count);
  const save = p.request("SAVE"); p.accept(response("SAVE")); await save;
  allow = true; await until(() => sent.filter(x => x === "PING").length >= 2);
  h.stop(); const stopped = sent.length; await sleep(40); assert.equal(sent.length, stopped);
});

test("UNKNOWN_COMMAND disables heartbeat once and normal requests continue", async t => {
  const sent = []; let errors = 0;
  const p = createProtocol(async line => {
    sent.push(line.trim());
    p.accept(line === "PING\n" ? '@CONFIG {"ok":false,"error":"UNKNOWN_COMMAND"}\n' : response(line.trim().split(" ")[0]));
  });
  const h = createHeartbeat(p, () => true, () => ++errors, () => {}, 10);
  t.after(() => { h.stop(); p.close(); });
  h.start(); await sleep(50);
  assert.deepEqual(sent, ["PING"]); assert.equal(errors, 0);
  await p.request("SET pointerInvertX 1"); await p.request("SAVE");
  assert.deepEqual(sent, ["PING", "SET pointerInvertX 1", "SAVE"]);
});

test("UI heartbeat stops on disconnect/unplug and probes each reconnect", async t => {
  const ui = setupUI(true, true);
  t.after(() => ui.disconnect());
  const first = await ui.connect();
  assert.deepEqual(first.allCommands, ["GET", "PING"]);
  await until(() => first.allCommands.filter(x => x === "PING").length === 2, 2600);
  await ui.disconnect(); const count = first.allCommands.length;
  const second = await ui.connect();
  assert.deepEqual(second.allCommands, ["GET", "PING"]);
  second.unplug(); await until(() => ui.el("connection-status").textContent === "Disconnected");
  const secondCount = second.allCommands.length;
  await sleep(2100);
  assert.equal(first.allCommands.length, count); assert.equal(second.allCommands.length, secondCount);
  const third = await ui.connect(); assert.deepEqual(third.allCommands, ["GET", "PING"]);
});


test("user SET/SAVE drain immediately after an in-flight PING, without overlapping requests", async t => {
  const ui = setupUI(true, true, p => { p.holdPing = true; });
  t.after(() => ui.disconnect());
  const port = await ui.connect();
  ui.el("pointerInvertX").checked = true; ui.el("pointerInvertX").fire("input");
  ui.el("save").fire("click");
  await sleep(160);
  assert.deepEqual(port.allCommands, ["GET", "PING"]);
  port.emit('@CONFIG {"ok":true,"command":"PING"}\n');
  await until(() => ui.el("save-status").textContent === "Saved");
  assert.deepEqual(port.allCommands, ["GET", "PING", "SET pointerInvertX 1", "SAVE"]);
});

test("heartbeat timeout GET resync restores usable UI and ignores late PING", async t => {
  const ui = setupUI(true, true, p => { p.holdPing = true; });
  t.after(() => ui.disconnect());
  const port = await ui.connect();
  await until(() => port.commands.filter(x => x === "GET").length === 2, 2600);
  await until(() => ui.el("connection-status").textContent === "Connected");
  assert.equal(ui.el("pointer-controls").disabled, false);
  port.emit('@CONFIG {"ok":true,"command":"PING"}\n');
  ui.el("pointerInvertY").checked = true; ui.el("pointerInvertY").fire("input");
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  assert.equal(port.config.pointerInvertY, true);
});


function autoUI(ports, { manual = fakePort(), secure = true, getPorts } = {}) {
  const doc = fakeDocument();
  const serial = { ...eventTarget(), ports, requests: 0, discoveries: 0,
    async getPorts() { serial.discoveries++; return getPorts ? getPorts() : serial.ports; },
    async requestPort() { serial.requests++; return manual; }
  };
  mount(doc, serial, secure);
  const el = id => doc.getElementById(id);
  return { serial, el, async ready(state = "Disconnected", timeout = 3000) {
    await until(() => el("connection-status").textContent === state, timeout);
  }, async close() { el("disconnect").fire("click"); await this.ready(); } };
}

function assertReleased(port) {
  assert.equal(port.readable.locked, false);
  assert.equal(port.writable.locked, false);
}

test("authorized filtering requires VID only, handles absent info and deduplicates", () => {
  const a = fakePort(), b = fakePort(), c = fakePort(), d = fakePort();
  b.getInfo = () => ({ usbVendorId: 0x2e8a, usbProductId: 65535 });
  c.getInfo = () => ({}); d.getInfo = () => { throw new Error("gone"); };
  assert.deepEqual(authorizedCandidates([a, b, c, d, a]), [a, b]);
});

test("no authorized ports / wrong VID / absent VID never open or show picker", async () => {
  for (const info of [null, { usbVendorId: 1234 }, {}]) {
    const port = fakePort(); port.getInfo = () => info;
    const ui = autoUI(info === null ? [] : [port]);
    assert.equal(ui.el("connect").disabled, true);
    await ui.ready();
    assert.equal(port.openCalls, 0); assert.equal(ui.serial.requests, 0);
    assert.equal(ui.el("connect").disabled, false);
    assert.match(ui.el("message").textContent, /アクセスを許可/);
  }
});

test("single authorized port uses one open/GET then heartbeat, supports legacy PING", async t => {
  const port = fakePort(null, () => {}, true);
  port.getReply = ['@DEBUG boot\r\n@CONFIG invalid\n', response("GET", actionDefaults()).slice(0, 20), response("GET", actionDefaults()).slice(20)];
  const ui = autoUI([port]); t.after(() => ui.close());
  await ui.ready("Connected");
  assert.equal(port.openCalls, 1); assert.equal(port.closeCalls, 0);
  assert.deepEqual(port.allCommands, ["GET", "PING"]);
  assert.equal(ui.serial.requests, 0);
  assert.equal(ui.el("button-controls").disabled, false);
  ui.el("save").fire("click"); await until(() => ui.el("save-status").textContent === "Saved");
});

test("single authorized timeout and malformed-only response close and restore manual Connect", async () => {
  for (const reply of ['', '@DEBUG hi\n@CONFIG {oops}\n' + response("GET", { pointerInvertX: true })]) {
    const port = fakePort(); port.getReply = reply;
    const ui = autoUI([port]); await ui.ready();
    assert.equal(port.closed, true); assertReleased(port);
    assert.deepEqual(port.allCommands, ["GET"]);
    assert.equal(ui.el("connect").disabled, false);
    assert.equal(ui.el("message").dataset.kind, "info");
  }
});

test("multiple candidates probe serially, continue after timeout/open failure, reconnect unique identity", async t => {
  const failed = fakePort(); failed.openFailure = true;
  const other = fakePort(); other.getReply = '';
  const red = fakePort();
  let opened = 0;
  for (const port of [other, red]) {
    const open = port.open, close = port.close;
    port.open = async options => { assert.equal(opened, 0); await open(options); opened++; };
    port.close = async () => { await close(); opened--; };
  }
  const ui = autoUI([failed, other, red]); t.after(() => ui.close());
  await ui.ready("Connected");
  assertReleased(other); assert.equal(other.closed, true);
  assert.equal(red.openCalls, 2); assert.equal(red.closeCalls, 1);
  assert.deepEqual(red.allCommands, ["GET", "GET", "PING"]);
  assert.equal(opened, 1);
});

test("multiple RedPoints close every probe, report ambiguity and allow manual choice", async t => {
  const a = fakePort(), b = fakePort();
  const ui = autoUI([a, b], { manual: b }); t.after(() => ui.close());
  await ui.ready();
  assert.match(ui.el("message").textContent, /複数のRedPoint/);
  for (const port of [a, b]) { assertReleased(port); assert.equal(port.closed, true); assert.deepEqual(port.allCommands, ["GET"]); }
  ui.el("connect").fire("click"); await ui.ready("Connected");
  assert.equal(ui.serial.requests, 1); assert.equal(b.openCalls, 2);
});

test("multiple candidates with no identity remain disconnected", async () => {
  const ports = [fakePort(), fakePort()];
  for (const p of ports) p.openFailure = true;
  const ui = autoUI(ports); await ui.ready();
  assert.match(ui.el("message").textContent, /確認できません/);
  assert.equal(ui.serial.requests, 0);
});

test("manual non-RedPoint closes without retry; unknown VID remains usable manually", async t => {
  const invalid = fakePort(); invalid.getReply = '';
  const ui = autoUI([], { manual: invalid }); await ui.ready();
  ui.el("connect").fire("click"); await ui.ready();
  assert.equal(invalid.closed, true); assertReleased(invalid);
  assert.deepEqual(invalid.allCommands, ["GET"]);
  assert.match(ui.el("message").textContent, /RedPointとして接続できません/);
  const valid = fakePort(); valid.getInfo = () => ({});
  ui.serial.requestPort = async () => valid;
  ui.el("connect").fire("click"); t.after(() => ui.close());
  await ui.ready("Connected"); assert.equal(valid.openCalls, 1);
});

test("manual Disconnect suppresses USB auto reconnect until a fresh mount; manual Connect still works", async t => {
  const port = fakePort(); const ui = autoUI([port], { manual: port });
  await ui.ready("Connected"); await ui.close();
  ui.serial.fire("connect", { target: port }); await sleep(20);
  assert.equal(ui.serial.discoveries, 1); assert.equal(port.openCalls, 1);
  ui.el("connect").fire("click"); await ui.ready("Connected"); await ui.close();
  const fresh = autoUI([port]); t.after(() => fresh.close());
  await fresh.ready("Connected"); assert.equal(port.openCalls, 3);
});

test("USB connect events auto discover only while idle; manual clicks cannot race getPorts", async t => {
  let resolvePorts;
  const port = fakePort();
  const ui = autoUI([], { getPorts: () => new Promise(resolve => { resolvePorts = resolve; }) });
  t.after(() => ui.close());
  ui.el("connect").fire("click"); ui.serial.fire("connect", { target: port });
  assert.equal(ui.serial.requests, 0); assert.equal(ui.serial.discoveries, 1);
  resolvePorts([]); await ui.ready();
  ui.serial.getPorts = async () => [port];
  ui.serial.fire("connect", { target: port }); await ui.ready("Connected");
  ui.serial.fire("connect", { target: port }); assert.equal(port.openCalls, 1);
});

test("USB disconnect during probe advances to next candidate, stale events cannot kill new session", async t => {
  const old = fakePort(); old.hold = true;
  const next = fakePort();
  const ui = autoUI([old, next]); t.after(() => ui.close());
  await until(() => old.commands.includes("GET"));
  ui.serial.fire("disconnect", { target: old });
  await ui.ready("Connected");
  assert.equal(old.closed, true); assertReleased(old);
  ui.serial.fire("disconnect", { target: old });
  await sleep(10); assert.equal(ui.el("connection-status").textContent, "Connected");
  assert.equal(next.closed, false);
});

test("stream loss during probe closes candidate; close failure stops before next open", async () => {
  const a = fakePort(); a.hold = true;
  const b = fakePort();
  const ui = autoUI([a, b]); await until(() => a.commands.length === 1);
  a.unplug(); await ui.ready("Connected"); await ui.close(); assertReleased(a);
  const bad = fakePort(); bad.closeFailure = true;
  const untouched = fakePort(); const failed = autoUI([bad, untouched]); await failed.ready();
  assert.equal(untouched.openCalls, 0); assertReleased(bad);
  assert.match(failed.el("message").textContent, /close failed/);
});

test("Disconnect during pending open waits for release and ignores stale GET/callback", async () => {
  const port = fakePort(); const open = port.open; let releaseOpen;
  port.open = async options => { await new Promise(resolve => { releaseOpen = resolve; }); await open(options); };
  const ui = autoUI([port]); await until(() => releaseOpen);
  ui.el("disconnect").fire("click");
  ui.serial.fire("connect", { target: port }); ui.el("connect").fire("click");
  assert.equal(ui.serial.requests, 0);
  releaseOpen(); await ui.ready();
  assert.equal(port.closed, true); assertReleased(port);
  assert.deepEqual(port.allCommands, []);
});

test("late GET and closed connection callback cannot revive timed-out identity", async () => {
  const port = fakePort(); port.hold = true; let losses = 0;
  const active = createDeviceConnection(port, () => losses++, 20);
  await assert.rejects(active.openAndSync(), /TIMEOUT/); await active.close();
  active.protocol.accept(response()); active.fail(new Error("late"));
  assert.equal(losses, 0); assertReleased(port);
});

test("getPorts rejection, unsupported API and insecure contexts leave manual guidance", async () => {
  const ui = autoUI([], { getPorts: () => { throw new Error("permission unavailable"); } });
  await ui.ready(); assert.equal(ui.el("connect").disabled, false);
  const insecure = autoUI([], { secure: false });
  assert.equal(insecure.serial.discoveries, 0); assert.equal(insecure.el("connect").disabled, true);
  assert.match(insecure.el("message").textContent, /HTTPS/);
});


function httpDevice() {
  const device = { config: actionDefaults(), calls: [], fail: false, holdSet: false, releases: [] };
  device.fetch = async (url, options) => {
    device.calls.push({ url, ...options });
    assert.equal(url, "api/command");
    assert.equal(options.method, "POST");
    assert.equal(options.headers["Content-Type"], "text/plain");
    assert.equal(options.headers["Cache-Control"], "no-store");
    assert.equal(options.cache, "no-store");
    assert.equal(options.mode, "same-origin");
    if (device.fail) throw new Error("network lost");
    const [command, key, value] = options.body.trim().split(" ");
    if (device.reject) { device.reject = false; return httpReply('@CONFIG {"ok":false,"error":"INVALID_VALUE"}\n'); }
    if (command === "SET") device.config[key] = key.endsWith("Action") ? value : key.includes("Invert") ? value === "1" : Number(value);
    if (command === "RESET") device.config = actionDefaults();
    const reply = httpReply(command === "PING" ? '@CONFIG {"ok":true,"command":"PING"}\n' : response(command, { ...device.config }));
    if (command === "SET" && device.holdSet) return new Promise(resolve => device.releases.push(() => resolve(reply)));
    return reply;
  };
  return device;
}
function httpReply(text, status = 200) { return { ok: status >= 200 && status < 300, status, async text() { return text; } }; }
function httpUI(device, serial, secure = false) {
  const doc = fakeDocument();
  mount(doc, serial, secure, { fetch: device.fetch });
  const el = id => doc.getElementById(id);
  return { el, async ready(state = "Connected") { await until(() => el("connection-status").textContent === state, 3000); },
    async close() { el("disconnect").fire("click"); await this.ready("Disconnected"); } };
}

test("Pico backend captured GET/PING reaches Connected without Serial or secure context",
  { skip: !process.env.REDPOINT_PICO_GET_RESPONSE }, async t => {
    const fs = require("node:fs");
    const capture = process.env.REDPOINT_PICO_GET_RESPONSE;
    const replies = {
      GET: fs.readFileSync(capture, "utf8"),
      PING: fs.readFileSync(capture + ".ping", "utf8")
    };
    const config = JSON.parse(replies.GET.slice(8)).config;
    // Old Pages validates/extracts these names and ignores extra canonical fields.
    for (const key of ["pointerSensitivity", "middleSensitivity"]) {
      assert.equal(typeof config[key], "number");
      assert.ok(Number.isFinite(config[key]) && config[key] >= 0 && config[key] <= 10);
    }
    assert.equal(typeof config.invertX, "boolean");
    assert.equal(typeof config.invertY, "boolean");
    assert.equal(config.middleSensitivity, config.wheelSensitivityY);
    assert.equal(config.invertX, config.pointerInvertX);
    assert.equal(config.invertY, config.pointerInvertY);
    const calls = [];
    const ui = httpUI({ async fetch(url, options) {
      assert.equal(url, "api/command");
      assert.equal(options.method, "POST");
      assert.equal(options.mode, "same-origin");
      const command = options.body.trim();
      assert.ok(Object.hasOwn(replies, command));
      calls.push(command);
      return httpReply(replies[command]);
    }});
    t.after(() => ui.close());
    await ui.ready();
    assert.equal(ui.el("port-info").textContent, "USB Ethernet · IPv4 Link-Local");
    assert.deepEqual(calls, ["GET", "PING"]);
  });

test("HTTP probe works without Serial/secure context and shares GET/SET/RESET/SAVE/PING UI", async t => {
  const device = httpDevice(); const ui = httpUI(device); t.after(() => ui.close());
  await ui.ready();
  assert.equal(ui.el("port-info").textContent, "USB Ethernet · IPv4 Link-Local");
  assert.deepEqual(device.calls.map(c => c.body.trim()), ["GET", "PING"]);
  ui.el("pointerInvertX").checked = true; ui.el("pointerInvertX").fire("input"); ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  assert.equal(device.config.pointerInvertX, true);
  ui.el("reset").fire("click"); await until(() => device.calls.some(c => c.body.trim() === "RESET"));
  assert.equal(device.config.pointerInvertX, false);
});

test("HTTP failure falls back to existing authorized Serial discovery, never opens picker", async t => {
  const port = fakePort(); let discoveries = 0;
  const serial = { async getPorts() { discoveries++; return [port]; }, requestPort() { throw new Error("unexpected picker"); }, addEventListener() {} };
  const ui = httpUI({ fetch: async () => httpReply("Not found", 404) }, serial, true); t.after(() => ui.close());
  await ui.ready(); assert.equal(discoveries, 1); assert.equal(port.openCalls, 1);
  assert.match(ui.el("port-info").textContent, /USB 2e8a/);
});

test("malformed HTTP probe times out into Serial fallback; valid HTTP bypasses Serial", async t => {
  let discoveries = 0;
  const serial = { async getPorts() { discoveries++; return []; }, addEventListener() {} };
  const ui = httpUI({ fetch: async () => httpReply('@DEBUG hi\n@CONFIG {oops}\n' + response("GET", { pointerInvertX: true })) }, serial, true);
  await ui.ready("Disconnected"); assert.equal(discoveries, 1); assert.equal(ui.el("connect").disabled, false);
  const good = httpUI(httpDevice(), serial, true); t.after(() => good.close());
  await good.ready(); assert.equal(discoveries, 1);
});

test("HTTP protocol errors remain protocol errors; fetch/HTTP errors are transport failures", async () => {
  const device = httpDevice(); const active = createHttpConnection(device.fetch);
  await active.openAndSync(); device.reject = true;
  await assert.rejects(active.protocol.request("SET pointerInvertX 1"), error => error.code === "INVALID_VALUE");
  assert.equal(active.protocol.isIdle(), true);
  device.fail = true; await assert.rejects(active.protocol.request("GET"), /WRITE_FAILED.*network lost/);
  await active.close();
  const failed = createHttpConnection(async () => httpReply("bad", 503));
  await assert.rejects(failed.openAndSync(), /WRITE_FAILED.*503/); await failed.close();
});

test("late HTTP GET cannot complete a newer GET resync, even if fetch ignores abort", async () => {
  const resolvers = [];
  const active = createHttpConnection(() => new Promise(resolve => resolvers.push(resolve)), 35);
  await assert.rejects(active.openAndSync(), /TIMEOUT/);
  let done = false;
  const retry = active.protocol.request("GET", { resync: true }).then(value => { done = true; return value; });
  await until(() => resolvers.length === 2);
  resolvers[0](httpReply(response("GET", { ...actionDefaults(), pointerSensitivity: 9 })));
  await sleep(5); assert.equal(done, false);
  resolvers[1](httpReply(response("GET", actionDefaults())));
  assert.equal((await retry).pointerSensitivity, 1);
  await active.close();
});

test("HTTP SET timeout uses existing GET resync and ignores late SET body", async t => {
  const device = httpDevice(); const ui = httpUI(device); t.after(() => ui.close()); await ui.ready();
  device.holdSet = true;
  ui.el("pointerInvertX").checked = true; ui.el("pointerInvertX").fire("input");
  await until(() => device.calls.filter(c => c.body.trim() === "GET").length === 2, 2800);
  await ui.ready(); assert.match(ui.el("pointerInvertX-confirmed").textContent, /On/);
  device.releases[0](); await sleep(10);
  assert.equal(ui.el("connection-status").textContent, "Connected");
});

test("HTTP Disconnect aborts pending fetch; reconnect starts a new GET without permission", async t => {
  const device = httpDevice(); const ui = httpUI(device); t.after(() => ui.close()); await ui.ready();
  device.holdSet = true; ui.el("pointerInvertY").checked = true; ui.el("pointerInvertY").fire("input");
  await until(() => device.releases.length === 1);
  const old = device.calls.find(c => c.body.startsWith("SET"));
  await ui.close(); assert.equal(old.signal.aborted, true);
  ui.el("connect").fire("click"); await ui.ready();
  device.releases[0](); await sleep(10);
  assert.equal(ui.el("connection-status").textContent, "Connected");
  assert.equal(device.calls.filter(c => c.body.trim() === "GET").length, 2);
});

test("HTTP fetch loss disconnects and keeps HTTP reconnect available on iPad", async t => {
  const device = httpDevice(); const ui = httpUI(device); t.after(() => ui.close()); await ui.ready();
  device.fail = true; ui.el("save").fire("click"); await ui.ready("Disconnected");
  assert.equal(ui.el("connect").disabled, false);
  device.fail = false; ui.el("connect").fire("click"); await ui.ready();
});


test("C.2 validates independent axes and requires the complete new schema", () => {
  assert.ok(validConfig(actionDefaults()));
  for (const key of ["wheelSensitivityX", "wheelSensitivityY"]) {
    for (const value of [-1, 11, NaN, Infinity, "0.4"]) assert.ok(!validConfig({ ...defaults(), [key]: value }));
    assert.ok(validConfig({ ...defaults(), [key]: 0 }));
  }
  for (const key of ["pointerInvertX", "pointerInvertY", "wheelInvertX", "wheelInvertY"]) {
    assert.ok(!validConfig({ ...defaults(), [key]: 1 }));
    const missing = defaults(); delete missing[key]; assert.ok(!validConfig(missing));
  }
  assert.ok(!validConfig({pointerSensitivity:1,middleSensitivity:0.4,invertX:false,invertY:false}));
});

test("C.2 UI edits target each canonical field and RESET restores separate defaults", async () => {
  const ui = setupUI(true);
  await ui.connect();
  const port = ui.ports[0];
  for (const [key,value] of Object.entries({pointerSensitivity:2,pointerInvertX:true,pointerInvertY:true,
      wheelSensitivityX:0,wheelSensitivityY:0.75,wheelInvertX:true,wheelInvertY:true})) {
    const control = ui.el(key);
    if (typeof value === "boolean") control.checked = value; else control.value = String(value);
    control.fire("input");
    await until(() => port.config[key] === value);
    await until(() => !ui.el(`${key}-confirmed`).textContent.includes("反映待ち"));
    assert.ok(port.commands.includes(`SET ${key} ${typeof value === "boolean" ? 1 : value.toFixed(2)}`));
  }
  ui.el("reset").fire("click"); await until(() => port.commands.includes("RESET") && !ui.el("reset").disabled);
  assert.deepEqual(port.config, actionDefaults());
  assert.equal(ui.el("wheel-controls").disabled, false);
  ui.el("disconnect").fire("click"); await until(() => ui.el("connect").disabled === false);
  assert.equal(ui.el("wheel-controls").disabled, true);
});

test("C.2 HTML wires controls into Pointer/Wheel/Buttons with unique IDs", () => {
  const fs = require("node:fs");
  const html = fs.readFileSync(require("node:path").join(__dirname,"../configurator/index.html"),"utf8");
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]);
  assert.equal(new Set(ids).size,ids.length);
  const groups = {"pointer-controls":["pointerSensitivity","pointerInvertX","pointerInvertY"],
    "wheel-controls":["wheelSensitivityX","wheelSensitivityY","wheelInvertX","wheelInvertY"],
    "button-controls":["leftAction","middleAction","rightAction"]};
  for(const [id,fields] of Object.entries(groups)) {
    const body = html.match(new RegExp(`<fieldset id="${id}" disabled>([\\s\\S]*?)</fieldset>`))?.[1];
    assert.ok(body,id);
    for(const field of fields) assert.ok(body.includes(`id="${field}"`),field);
  }
  assert.ok(html.indexOf('id="pointer-title"') < html.indexOf('id="wheel-title"'));
  assert.ok(html.indexOf('id="wheel-title"') < html.indexOf('id="buttons-title"'));
});

test("C.3 shortcut controls expand only for keyboard choices without changing capture behavior", async () => {
  const ui=setupUI(true); const port=await ui.connect();
  for(const key of ["leftAction","middleAction","rightAction"]) assert.equal(ui.el(`${key}-shortcut`).hidden,true);
  assert.equal(ui.el("message").hidden,true);
  ui.el("leftAction").value="shortcut"; ui.el("leftAction").fire("change");
  assert.equal(ui.el("leftAction-shortcut").hidden,false);
  assert.equal(ui.el("middleAction-shortcut").hidden,true);
  assert.deepEqual(port.commands,["GET"]); // choosing a UI category is not an invalid SET
  ui.el("leftAction-record").fire("click");
  ui.doc.fire("keydown",keyEvent("KeyX",{ctrlKey:true,shiftKey:true}));
  await until(()=>port.config.leftAction==="key:03:1B");
  assert.equal(ui.el("leftAction-shortcut").hidden,false);
  assert.match(ui.el("leftAction-shortcut-value").textContent,/Ctrl \+ Shift \+ X/);
  ui.el("leftAction").value="disabled"; ui.el("leftAction").fire("change");
  await until(()=>port.config.leftAction==="disabled");
  assert.equal(ui.el("leftAction-shortcut").hidden,true);
  ui.el("rightAction").value="shortcut"; ui.el("rightAction").fire("change");
  ui.el("rightAction-record").fire("click"); ui.el("recorder-cancel").fire("click");
  assert.equal(port.config.rightAction,"mouse:right");
  ui.el("reset").fire("click"); await until(()=>!ui.el("reset").disabled);
  assert.equal(ui.el("rightAction-shortcut").hidden,true);
  await ui.disconnect();
});

test("C.3 compact toolbar and exactly three cards preserve all controls", () => {
  const html=require("node:fs").readFileSync(require("node:path").join(__dirname,"../configurator/index.html"),"utf8");
  const toolbar=html.match(/<header class="toolbar"[\s\S]*?<\/header>/)[0];
  for(const id of ["connection-status","connect","disconnect","reset","save","save-status"]) assert.ok(toolbar.includes(`id="${id}"`));
  assert.equal((html.match(/<section class="card/g)||[]).length,3);
  assert.ok(!html.includes('class="brand"') && !html.includes('class="intro"'));
  for(const key of ["leftAction","middleAction","rightAction"]) assert.ok(html.includes(`id="${key}-shortcut" class="shortcut-controls" hidden`));
});
