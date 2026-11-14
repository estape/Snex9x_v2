// Snex9x v2 — Main Frontend View Orchestrator (MVC View Layer)

import { ipc } from "./ipc.js";
import { WebGLRenderer } from "./webgl-renderer.js";
import { state } from "./state.js";
import { initThemeEvents } from "./theme.js";
import { initMenubar, syncMenuIndicators } from "./menubar.js";
import {
  closeTopLayer,
  showToast,
  openGraphicsModal,
  openEmulationModal,
  openDatabaseModal,
  renderDatabaseSearchResults,
  initModalsEvents
} from "./modals.js";
import {
  openControlsModal,
  renderControlsModalPlayer,
  stopListeningForBind,
  applyCapturedBinding,
  initControlsEvents
} from "./controls.js";
import {
  filterAndRenderLibrary,
  selectGame,
  renderSelectedGameDetail,
  handleLibraryGamepadNav,
  initLibraryEvents
} from "./library.js";

const canvasEl = document.getElementById("snes-canvas");
const stageEl = document.getElementById("canvas-stage");
const renderer = new WebGLRenderer(canvasEl, stageEl, (fps, w, h) => {
  const fpsOverlay = document.getElementById("fps-overlay");
  if (fpsOverlay && !fpsOverlay.hidden) {
    document.getElementById("fps-counter-text").textContent = `${fps.toFixed(1)} FPS`;
    document.getElementById("res-counter-text").textContent = `${w}×${h}`;
  }
});

/**
 * Dispatches top menubar and global keyboard shortcut commands to the backend
 * or appropriate settings modal.
 * @param {string} action
 */
export function handleMenuAction(action) {
  if (document.querySelector("dialog.modal-dialog[open]")) {
    return;
  }
  switch (action) {
    case "open-rom":
      ipc.send("openRomDialog", { autoStart: true });
      break;
    case "scan-folder":
      ipc.send("scanRomFolderDialog");
      break;
    case "open-database":
      openDatabaseModal();
      break;
    case "close-rom":
      ipc.send("closeRom");
      break;
    case "exit-app":
      ipc.send("exitApp");
      break;
    case "toggle-pause":
      ipc.send("togglePause");
      break;
    case "soft-reset":
      ipc.send("softReset");
      break;
    case "hard-reset":
      ipc.send("hardReset");
      break;
    case "quick-save-state":
      ipc.send("saveStateSlot", { slot: state.settings.emulation.activeStateSlot });
      break;
    case "quick-load-state":
      ipc.send("loadStateSlot", { slot: state.settings.emulation.activeStateSlot });
      break;
    case "save-state-file":
      ipc.send("saveStateFileDialog");
      break;
    case "load-state-file":
      ipc.send("loadStateFileDialog");
      break;
    case "toggle-fullscreen":
      ipc.send("toggleFullscreen");
      break;
    case "open-graphics-settings":
      openGraphicsModal();
      break;
    case "open-emulation-settings":
      openEmulationModal();
      break;
    case "open-controls-settings":
      openControlsModal();
      break;
    case "refresh-devices":
      ipc.send("refreshDevices");
      showToast("Rescanned SDL3 gamepads.");
      break;
  }
}

/**
 * Toggles between #library-view and #emulator-view (guarded with prefers-reduced-motion
 * around View Transitions), updates paused overlay state, and starts/stops the renderer.
 */
export function syncWorkspaceMode() {
  const libView = document.getElementById("library-view");
  const emuView = document.getElementById("emulator-view");
  const returnBtn = document.getElementById("btn-return-library");
  const pausedOverlay = document.getElementById("paused-overlay");

  const isRunning = state.emulation.running;
  pausedOverlay.hidden = !state.emulation.paused || !isRunning;
  returnBtn.hidden = !isRunning;

  const applySwitch = () => {
    if (isRunning) {
      libView.hidden = true;
      emuView.hidden = false;
      renderer.start();
      renderer.updateViewportLayout();
      requestAnimationFrame(() => {
        renderer.updateViewportLayout();
      });
    } else {
      renderer.stop();
      emuView.hidden = true;
      libView.hidden = false;
    }
  };

  const prefersReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!prefersReducedMotion && document.startViewTransition && libView.hidden === isRunning) {
    document.startViewTransition(applySwitch);
  } else {
    applySwitch();
  }
}

/**
 * Subscribes to native WebView2 / Photino shared buffer and JSON IPC notifications.
 */
export function bindIpcHandlers() {
  ipc.onSharedVideoBuffer((buf) => {
    renderer.attachSharedBuffer(buf);
  });

  ipc.on("appState", (data) => {
    if (Array.isArray(data.library)) state.library = data.library;
    if (data.settings) state.settings = data.settings;
    if (data.controls) state.controls = data.controls;
    if (Array.isArray(data.devices)) state.devices = data.devices;
    if (data.emulation) state.emulation = data.emulation;

    syncMenuIndicators(renderer);
    filterAndRenderLibrary();
    syncWorkspaceMode();
  });

  ipc.on("boxartReady", (data) => {
    const game = state.library.find((g) => g.id === data.gameId || g.boxartFilename === data.boxartFilename);
    if (game) {
      game.boxartUrl = data.boxartUrl;
      if (game.id === state.selectedGameId) {
        renderSelectedGameDetail();
      }
    }
  });

  ipc.on("gameAdded", (data) => {
    if (data.gameId) {
      selectGame(data.gameId);
    }
  });

  ipc.on("devicesUpdated", (data) => {
    if (Array.isArray(data.devices)) {
      state.devices = data.devices;
      syncMenuIndicators(renderer);
      if (document.getElementById("dialog-controls").open) {
        renderControlsModalPlayer(state.editingPlayerTab);
      }
    }
  });

  ipc.on("gamepadButtonCaptured", (data) => {
    if (data.button && state.listeningButtonName) {
      applyCapturedBinding(data.button);
    }
  });

  ipc.on("databaseSearchResults", (data) => {
    renderDatabaseSearchResults(data.results);
  });

  ipc.on("toast", (data) => {
    if (data.message) showToast(data.message);
  });

  ipc.on("navLibrary", (data) => {
    handleLibraryGamepadNav(data);
  });
}

/**
 * Registers global keyboard listeners for button rebind capture, open modal Escape
 * dismissal, emulator shortcuts, and gameplay keyState forwarding.
 */
export function initGlobalKeyboardShortcuts() {
  window.addEventListener("keydown", (e) => {
    if (state.listeningButtonName) {
      e.preventDefault();
      e.stopPropagation();
      if (e.key === "Escape") {
        stopListeningForBind();
      } else {
        applyCapturedBinding(e.code);
      }
      return;
    }

    const openModal = document.querySelector("dialog.modal-dialog[open]");
    if (openModal) {
      if (e.key === "Escape") {
        e.preventDefault();
        closeTopLayer(openModal);
      }
      return;
    }

    const activeTag = document.activeElement ? document.activeElement.tagName : "";
    const inTextInput = activeTag === "INPUT" || activeTag === "TEXTAREA" || activeTag === "SELECT";

    if (e.ctrlKey && e.shiftKey && e.code === "KeyO") {
      e.preventDefault();
      handleMenuAction("scan-folder");
      return;
    }
    if (e.ctrlKey && e.code === "KeyO") {
      e.preventDefault();
      handleMenuAction("open-rom");
      return;
    }
    if (e.ctrlKey && e.code === "KeyD") {
      e.preventDefault();
      handleMenuAction("open-database");
      return;
    }
    if (e.ctrlKey && e.code === "KeyW") {
      e.preventDefault();
      handleMenuAction("close-rom");
      return;
    }
    if (e.ctrlKey && e.shiftKey && e.code === "KeyR") {
      e.preventDefault();
      handleMenuAction("hard-reset");
      return;
    }
    if (e.ctrlKey && e.code === "KeyR") {
      e.preventDefault();
      handleMenuAction("soft-reset");
      return;
    }
    if (e.code === "F5") {
      e.preventDefault();
      handleMenuAction("quick-save-state");
      return;
    }
    if (e.code === "F7") {
      e.preventDefault();
      handleMenuAction("quick-load-state");
      return;
    }
    if (e.code === "F9") {
      e.preventDefault();
      handleMenuAction("open-emulation-settings");
      return;
    }
    if (e.code === "F10") {
      e.preventDefault();
      handleMenuAction("open-graphics-settings");
      return;
    }
    if (e.code === "F11") {
      e.preventDefault();
      return;
    }
    if (e.code === "F12") {
      e.preventDefault();
      handleMenuAction("open-controls-settings");
      return;
    }

    if (!inTextInput && e.code === "Space" && state.emulation.running) {
      e.preventDefault();
      handleMenuAction("toggle-pause");
      return;
    }

    if (!inTextInput && state.emulation.running && !e.repeat) {
      ipc.send("keyState", { code: e.code, pressed: true });
    }
  });

  window.addEventListener("keyup", (e) => {
    if (state.emulation.running) {
      ipc.send("keyState", { code: e.code, pressed: false });
    }
  });
}

/**
 * Boots all UI modules, wires IPC and global shortcuts, and requests initial backend state.
 */
export function initApp() {
  initMenubar(handleMenuAction);
  initThemeEvents();
  initModalsEvents();
  initControlsEvents();
  initLibraryEvents({ onScanFolder: () => handleMenuAction("scan-folder") });
  initGlobalKeyboardShortcuts();
  bindIpcHandlers();
  ipc.send("initApp");
}

initApp();
