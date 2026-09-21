const { test } = require("node:test");
const assert = require("node:assert/strict");
const { parseLine, createLineReader, createProtocol, createSerialTransport, mount } = require("../configurator/app.js");

const defaults = () => ({ pointerSensitivity: 1, middleSensitivity: 0.4, invertX: false, invertY: false });
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

function fakePort(savedConfig = null, onSave = () => {}) {
  let controller;
  const port = {
    config: savedConfig ? { ...savedConfig } : defaults(), commands: [], writes: [], held: [], hold: false, rejectNext: false,
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
          if (command === "SET") port.config[key] = key.startsWith("invert") ? value === "1" : Number(value);
          if (command === "RESET") port.config = defaults();
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

function fakeDocument() {
  const elements = new Map();
  const doc = { getElementById(id) {
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

function setupUI() {
  const doc = fakeDocument();
  const ports = [];
  let persisted = null;
  const serial = {
    async requestPort() {
      const port = fakePort(persisted, config => { persisted = config; });
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
