// Snex9x v2 — 4-Player Controls & SDL3 Gamepad Rebinding Module

import { ipc } from "./ipc.js";
import { SNES_BUTTON_ORDER, state } from "./state.js";
import { closeAllMenus } from "./menubar.js";
import { closeTopLayer, registerModalBeforeCloseHook, updateModalState } from "./modals.js";

const GAMEPAD_POLL_INTERVAL_MS = 60;

/**
 * Opens the 4-Player Controller Configuration dialog and refreshes connected SDL3 devices.
 */
export function openControlsModal() {
  closeAllMenus();
  const dlg = document.getElementById("dialog-controls");
  ipc.send("refreshDevices");
  renderControlsModalPlayer(state.editingPlayerTab);
  dlg.showModal();
  updateModalState();
}

/**
 * Renders device selection, deadzone/rumble settings, and the 12 SNES button bindings
 * for the selected player port (1–4).
 * @param {number} playerNum
 */
export function renderControlsModalPlayer(playerNum) {
  state.editingPlayerTab = playerNum;
  stopListeningForBind();

  document.querySelectorAll(".player-tab").forEach((tab) => {
    tab.classList.toggle("active", parseInt(tab.dataset.playerTab, 10) === playerNum);
  });

  document.getElementById("active-player-heading").textContent = `Player ${playerNum}`;
  document.getElementById("controls-multitap-select").value = state.controls.multitapMode || "auto";

  const playerCfg = state.controls.players.find((p) => p.playerIndex === playerNum);
  if (!playerCfg) return;

  const deviceSelect = document.getElementById("player-device-select");
  deviceSelect.innerHTML = "";

  const noneOpt = document.createElement("option");
  noneOpt.value = "none";
  noneOpt.textContent = "Unassigned (Port Disconnected)";
  deviceSelect.appendChild(noneOpt);

  for (const dev of state.devices) {
    const opt = document.createElement("option");
    opt.value = dev.id;
    opt.textContent = dev.type === "keyboard" ? "Keyboard" : `${dev.name} (${dev.id})`;
    deviceSelect.appendChild(opt);
  }

  if (playerCfg.deviceType === "none") {
    deviceSelect.value = "none";
  } else if (playerCfg.deviceType === "keyboard") {
    deviceSelect.value = "keyboard";
  } else {
    deviceSelect.value = playerCfg.deviceId || "none";
  }

  const dzPercent = Math.round((playerCfg.deadzone || 0.2) * 100);
  document.getElementById("player-deadzone-range").value = String(dzPercent);
  document.getElementById("deadzone-value-label").textContent = `${dzPercent}%`;
  document.getElementById("player-rumble-checkbox").checked = Boolean(playerCfg.rumbleEnabled);

  const grid = document.getElementById("bindings-grid");
  grid.innerHTML = "";
  for (const btnName of SNES_BUTTON_ORDER) {
    const row = document.createElement("div");
    row.className = "bind-row";
    row.dataset.btnName = btnName;

    const label = document.createElement("span");
    label.className = "bind-btn-name";
    label.textContent = btnName;

    const keyBtn = document.createElement("button");
    keyBtn.type = "button";
    keyBtn.className = "bind-key-btn";
    keyBtn.textContent = (playerCfg.buttons && playerCfg.buttons[btnName]) || "Unbound";

    keyBtn.addEventListener("click", () => startListeningForBind(btnName, row, keyBtn));

    row.addEventListener("mouseenter", () => highlightSvgButton(btnName, true));
    row.addEventListener("mouseleave", () => highlightSvgButton(btnName, false));

    row.appendChild(label);
    row.appendChild(keyBtn);
    grid.appendChild(row);
  }
}

/**
 * Toggles the active highlight state on the matching SVG button inside the SNES pad graphic.
 * @param {string} btnName
 * @param {boolean} highlight
 */
export function highlightSvgButton(btnName, highlight) {
  const svgEl = document.querySelector(`[data-svg-btn="${btnName}"]`);
  if (svgEl) {
    svgEl.classList.toggle("highlight", highlight);
  }
}

/**
 * Enters key/gamepad rebind capture mode for a specific SNES button and begins
 * polling the assigned SDL3 gamepad device.
 * @param {string} btnName
 * @param {HTMLElement} rowEl
 * @param {HTMLButtonElement} keyBtnEl
 */
export function startListeningForBind(btnName, rowEl, keyBtnEl) {
  stopListeningForBind();
  state.listeningButtonName = btnName;
  rowEl.classList.add("listening");
  keyBtnEl.textContent = "Press Key/Pad...";
  highlightSvgButton(btnName, true);
  document.getElementById("rebind-prompt-banner").textContent =
    `Listening for ${btnName} on Player ${state.editingPlayerTab}... Press a key or gamepad button (Esc to cancel).`;

  const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
  const devId = playerCfg ? playerCfg.deviceId : "";
  state.gamepadPollTimer = setInterval(() => {
    ipc.send("pollGamepadBinding", { deviceId: devId });
  }, GAMEPAD_POLL_INTERVAL_MS);
}

/**
 * Stops active key/gamepad binding capture, clears polling timers, and restores UI hints.
 */
export function stopListeningForBind() {
  if (state.gamepadPollTimer) {
    clearInterval(state.gamepadPollTimer);
    state.gamepadPollTimer = null;
  }
  state.listeningButtonName = null;
  document.querySelectorAll(".bind-row.listening").forEach((el) => el.classList.remove("listening"));
  document.querySelectorAll("[data-svg-btn].highlight").forEach((el) => el.classList.remove("highlight"));
  const banner = document.getElementById("rebind-prompt-banner");
  if (banner) {
    banner.textContent = "Click any button on the right to rebind it via Keyboard or SDL3 Gamepad.";
  }
}

/**
 * Assigns a captured keyboard code or SDL3 gamepad button identifier to the active player
 * button currently being rebound and refreshes the controls grid.
 * @param {string} code
 */
export function applyCapturedBinding(code) {
  if (!state.listeningButtonName) return;
  const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
  if (playerCfg) {
    if (!playerCfg.buttons) playerCfg.buttons = {};
    playerCfg.buttons[state.listeningButtonName] = code;
  }
  stopListeningForBind();
  renderControlsModalPlayer(state.editingPlayerTab);
}

/**
 * Wires event handlers for player tabs, multitap mode, device assignment, deadzone/rumble,
 * gamepad rescan/test, save, and reset controls.
 */
export function initControlsEvents() {
  registerModalBeforeCloseHook(stopListeningForBind);

  document.querySelectorAll(".player-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      renderControlsModalPlayer(parseInt(tab.dataset.playerTab, 10));
    });
  });

  document.getElementById("controls-multitap-select").addEventListener("change", (e) => {
    state.controls.multitapMode = e.target.value;
    state.settings.emulation.multitapMode = e.target.value;
  });

  document.getElementById("player-device-select").addEventListener("change", (e) => {
    const val = e.target.value;
    const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
    if (!playerCfg) return;
    if (val === "none") {
      playerCfg.deviceType = "none";
      playerCfg.deviceId = "";
      playerCfg.deviceName = "Unassigned";
    } else if (val === "keyboard") {
      playerCfg.deviceType = "keyboard";
      playerCfg.deviceId = "keyboard";
      playerCfg.deviceName = "Keyboard";
    } else {
      const dev = state.devices.find((d) => d.id === val);
      playerCfg.deviceType = "gamepad";
      playerCfg.deviceId = val;
      playerCfg.deviceName = dev ? dev.name : val;
    }
  });

  document.getElementById("player-deadzone-range").addEventListener("input", (e) => {
    const val = parseInt(e.target.value, 10);
    document.getElementById("deadzone-value-label").textContent = `${val}%`;
    const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
    if (playerCfg) playerCfg.deadzone = val / 100.0;
  });

  document.getElementById("player-rumble-checkbox").addEventListener("change", (e) => {
    const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
    if (playerCfg) playerCfg.rumbleEnabled = e.target.checked;
  });

  document.getElementById("btn-rescan-pads").addEventListener("click", () => {
    ipc.send("refreshDevices");
  });

  document.getElementById("btn-test-rumble").addEventListener("click", () => {
    ipc.send("testRumble", { playerIndex: state.editingPlayerTab });
  });

  document.getElementById("btn-save-controls").addEventListener("click", () => {
    ipc.send("updateControls", state.controls);
    closeTopLayer(document.getElementById("dialog-controls"));
  });

  document.getElementById("btn-reset-controls").addEventListener("click", () => {
    ipc.send("resetControls");
  });
}
