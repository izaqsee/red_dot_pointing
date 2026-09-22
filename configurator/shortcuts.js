"use strict";

(() => {
  // KeyboardEvent.code -> USB HID Keyboard page 0x07 -> physical key label.
  // No layout-dependent event.key, ASCII conversion, consumer-page or JIS guesses.
  const CODE_MAP = Object.freeze({
    KeyA: [0x04, "A"], KeyB: [0x05, "B"], KeyC: [0x06, "C"], KeyD: [0x07, "D"],
    KeyE: [0x08, "E"], KeyF: [0x09, "F"], KeyG: [0x0A, "G"], KeyH: [0x0B, "H"],
    KeyI: [0x0C, "I"], KeyJ: [0x0D, "J"], KeyK: [0x0E, "K"], KeyL: [0x0F, "L"],
    KeyM: [0x10, "M"], KeyN: [0x11, "N"], KeyO: [0x12, "O"], KeyP: [0x13, "P"],
    KeyQ: [0x14, "Q"], KeyR: [0x15, "R"], KeyS: [0x16, "S"], KeyT: [0x17, "T"],
    KeyU: [0x18, "U"], KeyV: [0x19, "V"], KeyW: [0x1A, "W"], KeyX: [0x1B, "X"],
    KeyY: [0x1C, "Y"], KeyZ: [0x1D, "Z"],
    Digit1: [0x1E, "1"], Digit2: [0x1F, "2"], Digit3: [0x20, "3"], Digit4: [0x21, "4"],
    Digit5: [0x22, "5"], Digit6: [0x23, "6"], Digit7: [0x24, "7"], Digit8: [0x25, "8"],
    Digit9: [0x26, "9"], Digit0: [0x27, "0"],
    Enter: [0x28, "Enter"], Escape: [0x29, "Escape"], Backspace: [0x2A, "Backspace"],
    Tab: [0x2B, "Tab"], Space: [0x2C, "Space"], Minus: [0x2D, "Minus (-)"],
    Equal: [0x2E, "Equal (=)"], BracketLeft: [0x2F, "BracketLeft ([)"],
    BracketRight: [0x30, "BracketRight (])"], Backslash: [0x31, "Backslash (\\)"],
    Semicolon: [0x33, "Semicolon (;)"], Quote: [0x34, "Quote (')"], Backquote: [0x35, "Backquote (`)"],
    Comma: [0x36, "Comma (,)"], Period: [0x37, "Period (.)"], Slash: [0x38, "Slash (/)"],
    CapsLock: [0x39, "CapsLock"],
    F1: [0x3A, "F1"], F2: [0x3B, "F2"], F3: [0x3C, "F3"], F4: [0x3D, "F4"],
    F5: [0x3E, "F5"], F6: [0x3F, "F6"], F7: [0x40, "F7"], F8: [0x41, "F8"],
    F9: [0x42, "F9"], F10: [0x43, "F10"], F11: [0x44, "F11"], F12: [0x45, "F12"],
    PrintScreen: [0x46, "PrintScreen"], ScrollLock: [0x47, "ScrollLock"], Pause: [0x48, "Pause"],
    Insert: [0x49, "Insert"], Home: [0x4A, "Home"], PageUp: [0x4B, "PageUp"],
    Delete: [0x4C, "Delete"], End: [0x4D, "End"], PageDown: [0x4E, "PageDown"],
    ArrowRight: [0x4F, "Right"], ArrowLeft: [0x50, "Left"], ArrowDown: [0x51, "Down"], ArrowUp: [0x52, "Up"],
    NumLock: [0x53, "NumLock"], NumpadDivide: [0x54, "Numpad /"], NumpadMultiply: [0x55, "Numpad *"],
    NumpadSubtract: [0x56, "Numpad -"], NumpadAdd: [0x57, "Numpad +"], NumpadEnter: [0x58, "Numpad Enter"],
    Numpad1: [0x59, "Numpad 1"], Numpad2: [0x5A, "Numpad 2"], Numpad3: [0x5B, "Numpad 3"],
    Numpad4: [0x5C, "Numpad 4"], Numpad5: [0x5D, "Numpad 5"], Numpad6: [0x5E, "Numpad 6"],
    Numpad7: [0x5F, "Numpad 7"], Numpad8: [0x60, "Numpad 8"], Numpad9: [0x61, "Numpad 9"],
    Numpad0: [0x62, "Numpad 0"], NumpadDecimal: [0x63, "Numpad ."],
    IntlBackslash: [0x64, "IntlBackslash (ISO)"], ContextMenu: [0x65, "ContextMenu"], NumpadEqual: [0x67, "Numpad ="],
    F13: [0x68, "F13"], F14: [0x69, "F14"], F15: [0x6A, "F15"], F16: [0x6B, "F16"],
    F17: [0x6C, "F17"], F18: [0x6D, "F18"], F19: [0x6E, "F19"], F20: [0x6F, "F20"],
    F21: [0x70, "F21"], F22: [0x71, "F22"], F23: [0x72, "F23"], F24: [0x73, "F24"]
  });
  const USAGE_LABELS = new Map(Object.values(CODE_MAP));
  const SIMPLE_ACTIONS = { "mouse:left": "Left Click", "mouse:middle": "Middle Click", "mouse:right": "Right Click", disabled: "Disabled" };
  const MODIFIER_CODES = new Set(["ControlLeft", "ControlRight", "ShiftLeft", "ShiftRight", "AltLeft", "AltRight", "MetaLeft", "MetaRight"]);
  const hex = value => value.toString(16).toUpperCase().padStart(2, "0");
  const modifiersFromEvent = event => (event.ctrlKey ? 1 : 0) | (event.shiftKey ? 2 : 0) | (event.altKey ? 4 : 0) | (event.metaKey ? 8 : 0);
  const modifierNames = bits => ["Ctrl", "Shift", "Alt", "Meta"].filter((_, bit) => bits & (1 << bit));

  function validAction(value) {
    if (Object.hasOwn(SIMPLE_ACTIONS, value)) return true;
    if (typeof value !== "string" || !/^key:[0-9A-F]{2}:[0-9A-F]{2}$/.test(value)) return false;
    return parseInt(value.slice(4, 6), 16) <= 15 && USAGE_LABELS.has(parseInt(value.slice(7), 16));
  }
  function actionLabel(value) {
    if (!validAction(value)) return "—";
    if (Object.hasOwn(SIMPLE_ACTIONS, value)) return SIMPLE_ACTIONS[value];
    return [...modifierNames(parseInt(value.slice(4, 6), 16)), USAGE_LABELS.get(parseInt(value.slice(7), 16))].join(" + ");
  }

  function createShortcutRecorder(target, { preview, commit, cancel }) {
    let active = false;
    function stop() {
      active = false;
      target.removeEventListener("keydown", keydown, true);
      target.removeEventListener("keyup", keyup, true);
      target.removeEventListener("visibilitychange", visibility, true);
      target.defaultView?.removeEventListener("blur", windowBlur, true);
    }
    function abort() { if (active) { stop(); cancel(); } }
    // Disabling the focused Record button also emits blur through window capture.
    // Only leaving the window should cancel recording, not changing element focus.
    function windowBlur(event) { if (event.target === target.defaultView) abort(); }
    function visibility() { if (target.hidden) abort(); }
    function capture(event) { event.preventDefault(); event.stopPropagation(); }
    function keydown(event) {
      if (!active) return;
      capture(event);
      if (event.repeat) return;
      if (event.isComposing || event.getModifierState?.("AltGraph")) {
        preview("IME / AltGraphは未対応です。別のキーを押すかCancelしてください。", true);
        return;
      }
      const modifiers = modifiersFromEvent(event);
      if (MODIFIER_CODES.has(event.code)) {
        preview(modifierNames(modifiers).join(" + ") || "キーを押してください…", false);
        return;
      }
      const key = Object.hasOwn(CODE_MAP, event.code) ? CODE_MAP[event.code] : null;
      if (!key) {
        preview(`未対応キー: ${event.code || "Unknown"}。割当は変更していません。`, true);
        return;
      }
      const value = `key:${hex(modifiers)}:${hex(key[0])}`;
      preview(actionLabel(value), false);
      stop(); // First non-modifier key only. Escape and Backspace are ordinary keys.
      commit(value);
    }
    function keyup(event) {
      if (!active) return;
      capture(event);
      if (MODIFIER_CODES.has(event.code)) preview(modifierNames(modifiersFromEvent(event)).join(" + ") || "キーを押してください…", false);
    }
    return {
      start() {
        stop();
        active = true;
        target.addEventListener("keydown", keydown, true);
        target.addEventListener("keyup", keyup, true);
        target.addEventListener("visibilitychange", visibility, true);
        target.defaultView?.addEventListener("blur", windowBlur, true);
        preview("キーを押してください…", false);
      },
      stop,
      cancel: abort
    };
  }
  const api = { CODE_MAP, validAction, actionLabel, createShortcutRecorder };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else window.RedPointShortcuts = api;
})();
