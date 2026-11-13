// Snex9x v2 — Top Categorized Dropdown Menubar Module

import { ipc } from "./ipc.js";
import { state } from "./state.js";
import { applyThemeMode, syncThemeControls } from "./theme.js";

const SAVE_SLOT_COUNT = 10;
let onMenuActionHandler = () => {};

/**
 * Collapses all top-level menubar dropdowns and updates aria-expanded attributes.
 */
export function closeAllMenus() {
  document.querySelectorAll(".menu-category").forEach((cat) => {
    cat.classList.remove("open");
    const btn = cat.querySelector(".menu-trigger");
    if (btn) btn.setAttribute("aria-expanded", "false");
  });
}

/**
 * Initializes menubar ARIA roles, roving keyboard navigation, save-state slot buttons,
 * setting radio/toggle bindings, and action dispatchers.
 * @param {(action: string) => void} onAction
 */
export function initMenubar(onAction) {
  onMenuActionHandler = typeof onAction === "function" ? onAction : () => {};

  const categories = Array.from(document.querySelectorAll(".menu-category"));
  const triggers = categories.map((cat) => cat.querySelector(".menu-trigger")).filter(Boolean);

  const getEnabledDropdownItems = (cat) =>
    Array.from(cat.querySelectorAll(".menu-dropdown button")).filter((btn) => !btn.disabled);

  const openCategoryMenu = (cat, { focusFirstItem = false } = {}) => {
    closeAllMenus();
    cat.classList.add("open");
    const trigger = cat.querySelector(".menu-trigger");
    if (trigger) {
      triggers.forEach((t) => {
        t.tabIndex = t === trigger ? 0 : -1;
      });
      trigger.setAttribute("aria-expanded", "true");
    }
    if (focusFirstItem) {
      const items = getEnabledDropdownItems(cat);
      if (items.length > 0) items[0].focus();
    }
  };

  // Assign ARIA menu roles to dropdown items for assistive tech
  categories.forEach((cat) => {
    const dropdown = cat.querySelector(".menu-dropdown");
    if (dropdown) dropdown.setAttribute("role", "menu");
    cat.querySelectorAll(".menu-dropdown button").forEach((btn) => {
      if (btn.classList.contains("menu-radio")) {
        btn.setAttribute("role", "menuitemradio");
        btn.setAttribute("aria-checked", btn.classList.contains("selected") ? "true" : "false");
      } else if (btn.classList.contains("menu-toggle")) {
        btn.setAttribute("role", "menuitemcheckbox");
        btn.setAttribute("aria-checked", btn.classList.contains("selected") ? "true" : "false");
      } else {
        btn.setAttribute("role", "menuitem");
      }
      btn.tabIndex = -1;
    });
  });

  categories.forEach((cat, idx) => {
    const trigger = cat.querySelector(".menu-trigger");
    if (!trigger) return;

    trigger.addEventListener("click", (e) => {
      e.stopPropagation();
      if (document.querySelector("dialog.modal-dialog[open]")) {
        return;
      }
      const isOpen = cat.classList.contains("open");
      if (isOpen) {
        closeAllMenus();
      } else {
        openCategoryMenu(cat);
      }
    });

    trigger.addEventListener("mouseenter", () => {
      if (document.querySelector("dialog.modal-dialog[open]")) {
        return;
      }
      const anyOpen = Boolean(document.querySelector(".menu-category.open"));
      if (anyOpen && !cat.classList.contains("open")) {
        openCategoryMenu(cat);
      }
    });

    trigger.addEventListener("keydown", (e) => {
      if (document.querySelector("dialog.modal-dialog[open]")) return;
      if (e.key === "ArrowRight") {
        e.preventDefault();
        const nextCat = categories[(idx + 1) % categories.length];
        const nextTrigger = nextCat.querySelector(".menu-trigger");
        if (cat.classList.contains("open")) {
          openCategoryMenu(nextCat, { focusFirstItem: true });
        } else if (nextTrigger) {
          triggers.forEach((t) => {
            t.tabIndex = t === nextTrigger ? 0 : -1;
          });
          nextTrigger.focus();
        }
      } else if (e.key === "ArrowLeft") {
        e.preventDefault();
        const prevCat = categories[(idx - 1 + categories.length) % categories.length];
        const prevTrigger = prevCat.querySelector(".menu-trigger");
        if (cat.classList.contains("open")) {
          openCategoryMenu(prevCat, { focusFirstItem: true });
        } else if (prevTrigger) {
          triggers.forEach((t) => {
            t.tabIndex = t === prevTrigger ? 0 : -1;
          });
          prevTrigger.focus();
        }
      } else if (e.key === "ArrowDown") {
        e.preventDefault();
        openCategoryMenu(cat, { focusFirstItem: true });
      } else if (e.key === "Escape" && cat.classList.contains("open")) {
        e.preventDefault();
        closeAllMenus();
      }
    });

    const dropdown = cat.querySelector(".menu-dropdown");
    if (dropdown) {
      dropdown.addEventListener("keydown", (e) => {
        const items = getEnabledDropdownItems(cat);
        const currentIdx = items.indexOf(document.activeElement);
        if (e.key === "ArrowDown") {
          e.preventDefault();
          if (items.length > 0) items[(currentIdx + 1) % items.length].focus();
        } else if (e.key === "ArrowUp") {
          e.preventDefault();
          if (items.length > 0) items[(currentIdx - 1 + items.length) % items.length].focus();
        } else if (e.key === "Home") {
          e.preventDefault();
          if (items.length > 0) items[0].focus();
        } else if (e.key === "End") {
          e.preventDefault();
          if (items.length > 0) items[items.length - 1].focus();
        } else if (e.key === "ArrowRight") {
          e.preventDefault();
          const nextCat = categories[(idx + 1) % categories.length];
          openCategoryMenu(nextCat, { focusFirstItem: true });
        } else if (e.key === "ArrowLeft") {
          e.preventDefault();
          const prevCat = categories[(idx - 1 + categories.length) % categories.length];
          openCategoryMenu(prevCat, { focusFirstItem: true });
        } else if (e.key === "Escape") {
          e.preventDefault();
          closeAllMenus();
          trigger.focus();
        }
      });
    }
  });

  document.addEventListener("click", () => closeAllMenus());

  // Populate Save State Slots 1..10
  const slotGrid = document.getElementById("menu-slot-grid");
  if (slotGrid) {
    slotGrid.innerHTML = "";
    for (let i = 1; i <= SAVE_SLOT_COUNT; i++) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "slot-btn";
      btn.dataset.slot = String(i);
      btn.textContent = String(i);
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        ipc.send("setStateSlot", { slot: i });
        closeAllMenus();
      });
      slotGrid.appendChild(btn);
    }
  }

  // Menu Action Buttons
  document.querySelectorAll("[data-action]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const action = btn.dataset.action;
      closeAllMenus();
      onMenuActionHandler(action);
    });
  });

  // Graphics Radio Menu Items
  document.querySelectorAll("[data-setting-gfx]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.dataset.settingGfx;
      const val = btn.dataset.value;
      state.settings.graphics[key] = val;
      ipc.send("updateSettings", { graphics: state.settings.graphics });
      closeAllMenus();
    });
  });

  // Graphics Toggle Menu Items
  document.querySelectorAll("[data-toggle-gfx]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.dataset.toggleGfx;
      state.settings.graphics[key] = !state.settings.graphics[key];
      ipc.send("updateSettings", { graphics: state.settings.graphics });
      closeAllMenus();
    });
  });

  // Color Theme Menu Items
  document.querySelectorAll("[data-setting-theme]").forEach((btn) => {
    btn.addEventListener("click", () => {
      applyThemeMode(btn.dataset.settingTheme);
      closeAllMenus();
    });
  });

  // Emulation Radio Menu Items
  document.querySelectorAll("[data-setting-emu]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.dataset.settingEmu;
      let val = btn.dataset.value;
      if (key === "speedMultiplier") val = parseInt(val, 10);
      state.settings.emulation[key] = val;
      ipc.send("updateSettings", { emulation: state.settings.emulation });
      closeAllMenus();
    });
  });

  document.getElementById("btn-return-library").addEventListener("click", () => {
    ipc.send("closeRom");
  });
}

/**
 * Synchronizes menubar radio/checkbox checks, active slot badge, emulation controls,
 * connected gamepad pill, FPS HUD visibility, theme controls, and renderer settings.
 * @param {import("./webgl-renderer.js").WebGLRenderer} renderer
 */
export function syncMenuIndicators(renderer) {
  const gfx = state.settings.graphics;
  const emu = state.settings.emulation;

  document.querySelectorAll("[data-setting-gfx]").forEach((btn) => {
    const key = btn.dataset.settingGfx;
    const isSelected = String(gfx[key]) === btn.dataset.value;
    btn.classList.toggle("selected", isSelected);
    btn.setAttribute("aria-checked", isSelected ? "true" : "false");
  });

  document.querySelectorAll("[data-toggle-gfx]").forEach((btn) => {
    const key = btn.dataset.toggleGfx;
    const isSelected = Boolean(gfx[key]);
    btn.classList.toggle("selected", isSelected);
    btn.setAttribute("aria-checked", isSelected ? "true" : "false");
  });

  document.querySelectorAll("[data-setting-emu]").forEach((btn) => {
    const key = btn.dataset.settingEmu;
    const isSelected = String(emu[key]) === btn.dataset.value;
    btn.classList.toggle("selected", isSelected);
    btn.setAttribute("aria-checked", isSelected ? "true" : "false");
  });

  document.querySelectorAll("#menu-slot-grid .slot-btn").forEach((btn) => {
    btn.classList.toggle("active", parseInt(btn.dataset.slot, 10) === emu.activeStateSlot);
  });

  document.getElementById("active-slot-pill").textContent = `Slot ${emu.activeStateSlot}`;

  const isRunning = state.emulation.running;
  document.getElementById("menu-close-rom").disabled = !isRunning;
  document.getElementById("menu-pause-resume").disabled = !isRunning;
  document.getElementById("menu-soft-reset").disabled = !isRunning;
  document.getElementById("menu-hard-reset").disabled = !isRunning;
  document.getElementById("menu-pause-label").textContent = state.emulation.paused
    ? "Resume Emulation"
    : "Pause Emulation";

  const padCount = state.devices.filter((d) => d.type === "gamepad").length;
  document.getElementById("gamepad-count-label").textContent =
    padCount > 0 ? `${padCount} SDL3 Gamepad${padCount > 1 ? "s" : ""} + KB` : "Keyboard Ready";

  document.getElementById("fps-overlay").hidden = !gfx.showFps || !isRunning;
  syncThemeControls();
  if (renderer) {
    renderer.applySettings(gfx);
  }
}
