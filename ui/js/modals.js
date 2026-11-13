// Snex9x v2 — Top-Layer Modals, Settings Dialogs, Database Explorer & Toasts

import { ipc } from "./ipc.js";
import { state } from "./state.js";
import { closeAllMenus } from "./menubar.js";
import { getStoredTheme, applyThemeMode } from "./theme.js";

const MODAL_STATE_SYNC_DELAY_MS = 10;
const TOAST_VISIBLE_MS = 3200;
const TOAST_REMOVE_DELAY_MS = 260;
const supportsAllowDiscrete = CSS.supports("transition-behavior", "allow-discrete");

let beforeCloseHook = null;

/**
 * Registers an optional cleanup callback invoked whenever any modal dialog closes.
 * @param {() => void} fn
 */
export function registerModalBeforeCloseHook(fn) {
  beforeCloseHook = typeof fn === "function" ? fn : null;
}

/**
 * Synchronizes the backend modal-open lock flag so background gamepad navigation
 * is suspended whenever any <dialog.modal-dialog[open]> is active.
 */
export function updateModalState() {
  setTimeout(() => {
    const hasOpenModal = Boolean(document.querySelector("dialog.modal-dialog[open]"));
    ipc.send("setModalOpen", { open: hasOpenModal });
  }, MODAL_STATE_SYNC_DELAY_MS);
}

/**
 * Closes a top-layer <dialog> element gracefully, using allow-discrete transitions
 * when supported or a fallback transitionend listener otherwise.
 * @param {HTMLDialogElement | null} el
 */
export function closeTopLayer(el) {
  if (!el || !el.open) return;
  beforeCloseHook?.();
  if (supportsAllowDiscrete) {
    el.close();
    updateModalState();
    return;
  }
  el.classList.add("is-closing");
  el.addEventListener(
    "transitionend",
    () => {
      el.classList.remove("is-closing");
      el.close();
      updateModalState();
    },
    { once: true }
  );
}

/**
 * Attaches close/cancel listeners and a pointerdown/pointerup bounds fallback for
 * backdrop light-dismiss on browsers lacking native closedby="any".
 * @param {HTMLDialogElement | null} dialog
 */
export function setupDialogLightDismiss(dialog) {
  if (!dialog) return;
  const handleNativeClose = () => {
    beforeCloseHook?.();
    updateModalState();
  };
  dialog.addEventListener("close", handleNativeClose);
  dialog.addEventListener("cancel", handleNativeClose);

  if (!("closedBy" in dialog)) {
    const isOutsideDialog = (e) => {
      if (e.target !== dialog) return false;
      const rect = dialog.getBoundingClientRect();
      return (
        e.clientY < rect.top ||
        e.clientY > rect.top + rect.height ||
        e.clientX < rect.left ||
        e.clientX > rect.left + rect.width
      );
    };
    let pointerDownOutside = false;
    dialog.addEventListener("pointerdown", (e) => {
      pointerDownOutside = isOutsideDialog(e);
    });
    dialog.addEventListener("pointerup", (e) => {
      if (pointerDownOutside && isOutsideDialog(e)) {
        closeTopLayer(dialog);
      }
      pointerDownOutside = false;
    });
  }
}

/**
 * Displays a transient toast notification in the bottom-right toast stack.
 * @param {string} message
 */
export function showToast(message) {
  const container = document.getElementById("toast-container");
  if (!container) return;
  const item = document.createElement("div");
  item.className = "toast-item";
  item.textContent = message;
  container.appendChild(item);
  setTimeout(() => {
    item.classList.add("is-fading-out");
    setTimeout(() => item.remove(), TOAST_REMOVE_DELAY_MS);
  }, TOAST_VISIBLE_MS);
}

/**
 * Opens the Graphics & Shader Settings modal populated with the current settings.
 */
export function openGraphicsModal() {
  closeAllMenus();
  const gfx = state.settings.graphics;
  document.getElementById("gfx-aspect-select").value = gfx.aspectRatio || "4:3";
  document.getElementById("gfx-cpu-filter-select").value = gfx.cpuFilter || "none";
  document.getElementById("gfx-theme-select").value = getStoredTheme() ?? "system";
  document.getElementById("gfx-integer-check").checked = Boolean(gfx.integerScaling);
  document.getElementById("gfx-bilinear-check").checked = Boolean(gfx.bilinearFiltering);
  document.getElementById("gfx-crt-check").checked = Boolean(gfx.crtShader);
  document.getElementById("gfx-overscan-check").checked = Boolean(gfx.overscanCrop);
  document.getElementById("gfx-fps-check").checked = Boolean(gfx.showFps);
  document.getElementById("gfx-fullscreen-check").checked = Boolean(gfx.fullscreen);
  document.getElementById("dialog-graphics").showModal();
  updateModalState();
}

/**
 * Opens the Emulation & Audio Settings modal populated with the current settings.
 */
export function openEmulationModal() {
  closeAllMenus();
  const emu = state.settings.emulation;
  document.getElementById("emu-region-select").value = emu.region || "auto";
  document.getElementById("emu-speed-select").value = String(emu.speedMultiplier || 1);
  document.getElementById("emu-superfx-select").value = String(emu.superFxOverclock || 100);
  document.getElementById("emu-interp-select").value = emu.audioInterpolation || "gaussian";
  document.getElementById("emu-volume-range").value = String(emu.volume ?? 85);
  document.getElementById("emu-volume-label").textContent = `${emu.volume ?? 85}%`;
  document.getElementById("emu-mute-check").checked = Boolean(emu.mute);
  document.getElementById("emu-flicker-check").checked = Boolean(emu.reduceSpriteFlicker);
  document.getElementById("emu-autosave-check").checked = Boolean(emu.autoSaveSram);
  document.getElementById("dialog-emulation").showModal();
  updateModalState();
}

/**
 * Opens the SNES Master Game Database Explorer modal and triggers a search query.
 */
export function openDatabaseModal() {
  closeAllMenus();
  const dlg = document.getElementById("dialog-database");
  dlg.showModal();
  updateModalState();
  const q = document.getElementById("db-modal-search-input").value.trim();
  ipc.send("searchDatabase", { query: q });
}

/**
 * Renders matching SNES catalog rows inside #db-modal-results using safe DOM APIs.
 * @param {Array<Record<string, any>>} results
 */
export function renderDatabaseSearchResults(results) {
  const container = document.getElementById("db-modal-results");
  if (!container) return;
  container.innerHTML = "";
  const list = Array.isArray(results) ? results : [];

  if (list.length === 0) {
    const emptyEl = document.createElement("p");
    emptyEl.className = "db-empty-message";
    emptyEl.textContent = "No matching SNES games found.";
    container.appendChild(emptyEl);
    return;
  }

  for (const item of list) {
    const row = document.createElement("div");
    row.className = "db-result-item";

    const infoWrapper = document.createElement("div");
    const titleEl = document.createElement("strong");
    titleEl.textContent = item.name || "";

    const metaEl = document.createElement("div");
    metaEl.className = "db-result-meta";
    metaEl.textContent = `${item.year || ""} • ${item.region || ""} • ${item.developer || "Nintendo"} • ${item.genre || ""}`;

    infoWrapper.appendChild(titleEl);
    infoWrapper.appendChild(metaEl);
    row.appendChild(infoWrapper);

    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn-secondary btn-sm";
    btn.textContent = "Link ROM...";
    btn.addEventListener("click", () => {
      closeTopLayer(document.getElementById("dialog-database"));
      ipc.send("addDatabaseGameToLibrary", { id: item.id, noIntroName: item.noIntroName });
    });
    row.appendChild(btn);
    container.appendChild(row);
  }
}

/**
 * Wires dialog light-dismiss handlers, close buttons, Graphics/Emulation save actions,
 * and Database Explorer search interactions.
 */
export function initModalsEvents() {
  document.querySelectorAll("dialog.modal-dialog").forEach((dlg) => {
    setupDialogLightDismiss(dlg);
  });

  document.querySelectorAll("[data-close-dialog]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const targetId = btn.dataset.closeDialog;
      closeTopLayer(document.getElementById(targetId));
    });
  });

  // Graphics Modal Save
  document.getElementById("btn-save-graphics").addEventListener("click", () => {
    applyThemeMode(document.getElementById("gfx-theme-select")?.value ?? "system");
    state.settings.graphics = {
      aspectRatio: document.getElementById("gfx-aspect-select").value,
      cpuFilter: document.getElementById("gfx-cpu-filter-select").value,
      integerScaling: document.getElementById("gfx-integer-check").checked,
      bilinearFiltering: document.getElementById("gfx-bilinear-check").checked,
      crtShader: document.getElementById("gfx-crt-check").checked,
      overscanCrop: document.getElementById("gfx-overscan-check").checked,
      showFps: document.getElementById("gfx-fps-check").checked,
      fullscreen: document.getElementById("gfx-fullscreen-check").checked,
      vsync: true
    };
    ipc.send("updateSettings", { graphics: state.settings.graphics });
  });

  // Emulation Modal Volume & Save
  document.getElementById("emu-volume-range").addEventListener("input", (e) => {
    document.getElementById("emu-volume-label").textContent = `${e.target.value}%`;
  });

  document.getElementById("btn-save-emulation").addEventListener("click", () => {
    state.settings.emulation = {
      ...state.settings.emulation,
      region: document.getElementById("emu-region-select").value,
      speedMultiplier: parseInt(document.getElementById("emu-speed-select").value, 10),
      superFxOverclock: parseInt(document.getElementById("emu-superfx-select").value, 10),
      audioInterpolation: document.getElementById("emu-interp-select").value,
      volume: parseInt(document.getElementById("emu-volume-range").value, 10),
      mute: document.getElementById("emu-mute-check").checked,
      reduceSpriteFlicker: document.getElementById("emu-flicker-check").checked,
      autoSaveSram: document.getElementById("emu-autosave-check").checked
    };
    ipc.send("updateSettings", { emulation: state.settings.emulation });
  });

  // Database Modal Search
  document.getElementById("btn-db-modal-search").addEventListener("click", () => {
    const q = document.getElementById("db-modal-search-input").value.trim();
    ipc.send("searchDatabase", { query: q });
  });

  document.getElementById("db-modal-search-input").addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      ipc.send("searchDatabase", { query: e.target.value.trim() });
    }
  });
}
