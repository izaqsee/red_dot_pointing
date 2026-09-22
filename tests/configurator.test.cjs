const { test } = require("node:test");
const assert = require("node:assert/strict");
const { parseLine, createLineReader, createProtocol, createSerialTransport, mount } = require("../configurator/app.js");

const defaults = () => ({ pointerSensitivity: 1, middleSensitivity: 0.4, invertX: false, invertY: false });
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
  for (const line of ["@CONFIG {", "@CONFIG null", '@CONFIG {"ok":true}', response("GET", { ...defaults(), invertX: 1 }).trim(), response("GET", { ...defaults(), pointerSensitivity: 11 }).trim()]) {
    assert.equal(parseLine(line).kind, "invalid");
  }
});

test("single request, mixed debug/invalid input, authoritative response and device error", async () => {
  const writes = [];
  const protocol = createProtocol(async line => writes.push(line), 100);
  const result = protocol.request("SET middleSensitivity 0.25");
  await assert.rejects(protocol.request("GET"), /BUSY/);
  protocol.accept('@DEBUG PTR anything\n@CONFIG invalid\nunknown\n');
  protocol.accept(response("GET")); // A different command cannot complete SET.
  protocol.accept(response("SET", { ...defaults(), middleSensitivity: 0.25 }));
  assert.equal((await result).middleSensitivity, 0.25);
  assert.deepEqual(writes, ["SET middleSensitivity 0.25\n"]);
  const rejected = protocol.request("SET invertX 1");
  protocol.accept('@CONFIG {"ok":false,"error":"INVALID_VALUE"}\n');
  await assert.rejects(rejected, /INVALID_VALUE/);
  protocol.close();
});

test("timeout blocks mutation until GET resync; late SET/errors do not resolve GET", async () => {
  const writes = [];
  const protocol = createProtocol(async line => writes.push(line), 25);
  await assert.rejects(protocol.request("SET invertX 1"), /TIMEOUT/);
  await assert.rejects(protocol.request("SET invertY 1"), /RESYNC_REQUIRED/);
  let settled = false;
  const get = protocol.request("GET", { resync: true }).then(config => { settled = true; return config; });
  protocol.accept(response("SET"));
  protocol.accept('@CONFIG {"ok":false,"error":"INVALID_LINE"}\n');
  await sleep(2);
  assert.equal(settled, false);
  protocol.accept(response("GET", { ...defaults(), invertX: true }));
  assert.equal((await get).invertX, true);
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
    config: savedConfig ? { ...savedConfig } : withActions ? actionDefaults() : defaults(), commands: [], writes: [], held: [], hold: false, rejectNext: false,
    opened: false, closed: false,
    readable: new ReadableStream({ start(c) { controller = c; } }),
    async open(options) { port.opened = true; port.options = options; },
    async close() {
      assert.equal(port.readable.locked, false);
      assert.equal(port.writable.locked, false);
      port.closed = true;
    },
    getInfo() { return { usbVendorId: 0x2e8a, usbProductId: 10 }; },
    emit(text) { controller.enqueue(new TextEncoder().encode(text)); },
    release() { for (const reply of port.held.splice(0)) port.emit(reply); },
    unplug() { controller.error(new Error("USB removed")); }
  };
  port.writable = new WritableStream({
    write(bytes) {
      const text = new TextDecoder().decode(bytes);
      port.writes.push(text);
      for (const line of text.split("\n").filter(Boolean)) {
        port.commands.push(line);
        const [command, key, value] = line.split(" ");
        let reply;
        if (port.rejectNext) {
          port.rejectNext = false;
          reply = '@CONFIG {"ok":false,"error":"INVALID_VALUE"}\n';
        } else {
          if (command === "SET") port.config[key] = key.endsWith("Action") ? value : key.startsWith("invert") ? value === "1" : Number(value);
          if (command === "RESET") port.config = withActions ? actionDefaults() : defaults();
          if (command === "SAVE") onSave({ ...port.config });
          reply = response(command, port.config);
        }
        if (port.hold) port.held.push(reply);
        else port.emit(reply);
      }
    }
  });
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
      type: id.startsWith("invert") ? "checkbox" : "range", value: "", checked: false,
      disabled: false, dataset: {}, textContent: "", events: {},
      addEventListener(type, handler) { this.events[type] = handler; },
      fire(type) { this.events[type]?.({ target: this }); }
    });
    return elements.get(id);
  } };
  return doc;
}

function setupUI(withActions = false) {
  const doc = fakeDocument();
  const ports = [];
  let persisted = null;
  const serial = {
    async requestPort() {
      const port = fakePort(persisted, config => { persisted = config; }, withActions);
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
  ui.el("invertX").checked = true;
  ui.el("invertX").fire("input");
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
  ui.el("invertY").checked = true;
  ui.el("invertY").fire("input");
  await until(() => ui.el("message").textContent.includes("INVALID_VALUE"));
  assert.equal(ui.el("invertY").checked, false);
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
  ui.el("invertX").checked = true;
  ui.el("invertX").fire("input");
  await until(() => port.commands.length === 2);
  ui.el("invertY").checked = true;
  ui.el("invertY").fire("input");
  await until(() => port.commands.at(-1) === "GET", 3000);
  assert.equal(ui.el("pointer-controls").disabled, true);
  port.hold = false;
  port.release(); // Includes the late SET, then the resync GET.
  await until(() => ui.el("connection-status").textContent === "Connected");
  assert.equal(ui.el("invertX").checked, true);
  assert.equal(ui.el("invertY").checked, false);
  assert.deepEqual(port.commands, ["GET", "SET invertX 1", "GET"]);
  await ui.disconnect();
});

test("UI failed resync disconnects, then a fresh connection starts with GET", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("invertX").checked = true;
  ui.el("invertX").fire("input");
  await until(() => ui.el("connection-status").textContent === "Disconnected", 5000);
  assert.equal(port.closed, true);
  assert.equal(ui.el("connect").disabled, false);
  assert.match(ui.el("message").textContent, /再同期できません/);
  const fresh = await ui.connect();
  assert.deepEqual(fresh.commands, ["GET"]);
  assert.equal(ui.el("invertX").checked, false);
  await ui.disconnect();
});

test("disconnect cancels in-flight requests and never leaks drafts to a new session", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  port.hold = true;
  ui.el("invertX").checked = true;
  ui.el("invertX").fire("input");
  await until(() => port.commands.length === 2);
  await ui.disconnect();
  const fresh = await ui.connect();
  assert.equal(ui.el("invertX").checked, false);
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
  ui.el("middleSensitivity").value = "0.25";
  ui.el("middleSensitivity").fire("input");
  ui.el("save").fire("click"); // Before debounce expires.
  await until(() => port.commands.length === 2);
  assert.equal(port.commands[1], "SET middleSensitivity 0.25");
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
  assert.equal(ui.el("middleSensitivity-value").textContent, "0.25×");
  assert.equal(ui.el("save-status").textContent, "保存状態未確認");
  await ui.disconnect();
});

test("SET and RESET change Saved to Unsaved without automatically sending SAVE", async () => {
  const ui = setupUI();
  const port = await ui.connect();
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  ui.el("invertX").checked = true;
  ui.el("invertX").fire("input");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  await until(() => ui.el("invertX-confirmed").textContent === "デバイス確認値: On");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  assert.equal(port.commands.filter(line => line === "SAVE").length, 1);
  ui.el("save").fire("click");
  await until(() => ui.el("save-status").textContent === "Saved");
  ui.el("reset").fire("click");
  await until(() => ui.el("invertX-confirmed").textContent === "デバイス確認値: Off");
  assert.equal(ui.el("save-status").textContent, "Unsaved changes");
  await ui.disconnect();
  await ui.connect(); // Mock reboot loads earlier saved value, not RESET's RAM default.
  assert.equal(ui.el("invertX").checked, true);
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
  ui.el("invertY").checked = true;
  ui.el("invertY").fire("input");
  ui.el("save").fire("click");
  await until(() => ui.el("message").textContent.includes("INVALID_VALUE"));
  assert.deepEqual(port.commands, ["GET", "SET invertY 1"]);
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
