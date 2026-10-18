// Snex9x v2 — Main Frontend View Controller (MVC View Layer)

import { ipc } from "./ipc.js";
import { WebGLRenderer } from "./webgl-renderer.js";

const SNES_BUTTON_ORDER = [
  "Up", "Down", "Left", "Right",
  "A", "B", "X", "Y",
  "L", "R", "Start", "Select"
];

const state = {
  library: [],
  filteredLibrary: [],
  selectedGameId: null,
  activeFilter: "all",
  searchQuery: "",
  settings: {
    graphics: {
      aspectRatio: "4:3",
      integerScaling: false,
      bilinearFiltering: false,
      cpuFilter: "none",
      crtShader: false,
      overscanCrop: true,
      vsync: true,
      showFps: false,
      fullscreen: false
    },
    emulation: {
      region: "auto",
      speedMultiplier: 1,
      superFxOverclock: 100,
      reduceSpriteFlicker: false,
      volume: 85,
      mute: false,
      audioInterpolation: "gaussian",
      autoSaveSram: true,
      multitapMode: "auto",
      activeStateSlot: 1
    }
  },
  controls: {
    multitapMode: "auto",
    players: []
  },
  devices: [],
  emulation: {
    running: false,
    paused: false,
    activeGameId: "",
    fps: 60.0
  },
  editingPlayerTab: 1,
  listeningButtonName: null,
  gamepadPollTimer: null
};

// Modern Web Guidance: animate-to-from-top-layer fallback helper
const supportsAllowDiscrete = CSS.supports("transition-behavior", "allow-discrete");

// Initialize WebGL2 Renderer
const canvasEl = document.getElementById("snes-canvas");
const stageEl = document.getElementById("canvas-stage");
const renderer = new WebGLRenderer(canvasEl, stageEl, (fps, w, h) => {
  const fpsOverlay = document.getElementById("fps-overlay");
  if (fpsOverlay && !fpsOverlay.hidden) {
    document.getElementById("fps-counter-text").textContent = `${fps.toFixed(1)} FPS`;
    document.getElementById("res-counter-text").textContent = `${w}×${h}`;
  }
});

ipc.onSharedVideoBuffer((buf) => {
  renderer.attachSharedBuffer(buf);
});

function updateModalState() {
  setTimeout(() => {
    const hasOpenModal = Boolean(document.querySelector("dialog.modal-dialog[open]"));
    ipc.send("setModalOpen", { open: hasOpenModal });
  }, 10);
}

function closeTopLayer(el) {
  if (!el || !el.open) return;
  stopListeningForBind();
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

// Modern Web Guidance: light-dismiss-a-dialog fallback for browsers without closedby="any"
function setupDialogLightDismiss(dialog) {
  if (!dialog) return;
  dialog.addEventListener("close", updateModalState);
  dialog.addEventListener("cancel", updateModalState);
  if (!("closedBy" in dialog)) {
    dialog.addEventListener("click", (e) => {
      if (e.target === dialog) {
        const rect = dialog.getBoundingClientRect();
        const clickedInDialog =
          rect.top <= e.clientY &&
          e.clientY <= rect.top + rect.height &&
          rect.left <= e.clientX &&
          e.clientX <= rect.left + rect.width;
        if (!clickedInDialog) {
          closeTopLayer(dialog);
        }
      }
    });
  }
}

function showToast(message) {
  const container = document.getElementById("toast-container");
  if (!container) return;
  const item = document.createElement("div");
  item.className = "toast-item";
  item.textContent = message;
  container.appendChild(item);
  setTimeout(() => {
    item.style.opacity = "0";
    item.style.transition = "opacity 0.25s ease";
    setTimeout(() => item.remove(), 260);
  }, 3200);
}

function closeAllMenus() {
  document.querySelectorAll(".menu-category").forEach((cat) => {
    cat.classList.remove("open");
    const btn = cat.querySelector(".menu-trigger");
    if (btn) btn.setAttribute("aria-expanded", "false");
  });
}

// ============================================================================
// TOP MENUBAR & SLOTS SETUP
// ============================================================================
function initMenubar() {
  const categories = document.querySelectorAll(".menu-category");
  let anyMenuOpen = false;

  categories.forEach((cat) => {
    const trigger = cat.querySelector(".menu-trigger");
    trigger.addEventListener("click", (e) => {
      e.stopPropagation();
      if (document.querySelector("dialog.modal-dialog[open]")) {
        return;
      }
      const isOpen = cat.classList.contains("open");
      closeAllMenus();
      if (!isOpen) {
        cat.classList.add("open");
        trigger.setAttribute("aria-expanded", "true");
        anyMenuOpen = true;
      }
    });

    trigger.addEventListener("mouseenter", () => {
      if (document.querySelector("dialog.modal-dialog[open]")) {
        return;
      }
      if (anyMenuOpen && !cat.classList.contains("open")) {
        closeAllMenus();
        cat.classList.add("open");
        trigger.setAttribute("aria-expanded", "true");
        anyMenuOpen = true;
      }
    });
  });

  document.addEventListener("click", () => closeAllMenus());

  // Populate Save State Slots 1..10
  const slotGrid = document.getElementById("menu-slot-grid");
  if (slotGrid) {
    slotGrid.innerHTML = "";
    for (let i = 1; i <= 10; i++) {
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
      handleMenuAction(action);
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

function handleMenuAction(action) {
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

function syncMenuIndicators() {
  const gfx = state.settings.graphics;
  const emu = state.settings.emulation;

  document.querySelectorAll("[data-setting-gfx]").forEach((btn) => {
    const key = btn.dataset.settingGfx;
    btn.classList.toggle("selected", String(gfx[key]) === btn.dataset.value);
  });

  document.querySelectorAll("[data-toggle-gfx]").forEach((btn) => {
    const key = btn.dataset.toggleGfx;
    btn.classList.toggle("selected", Boolean(gfx[key]));
  });

  document.querySelectorAll("[data-setting-emu]").forEach((btn) => {
    const key = btn.dataset.settingEmu;
    btn.classList.toggle("selected", String(emu[key]) === btn.dataset.value);
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
  renderer.applySettings(gfx);
}

// ============================================================================
// LIBRARY VIEW (LEFT LIST & RIGHT BOXART + INFO PANEL)
// ============================================================================
function filterAndRenderLibrary() {
  const q = state.searchQuery.trim().toLowerCase();
  state.filteredLibrary = state.library.filter((game) => {
    if (state.activeFilter === "installed" && !game.hasLocalRom) return false;
    if (state.activeFilter === "favorites" && !game.favorite) return false;
    if (!q) return true;
    return (
      (game.name && game.name.toLowerCase().includes(q)) ||
      (game.year && String(game.year).toLowerCase().includes(q)) ||
      (game.region && game.region.toLowerCase().includes(q)) ||
      (game.genre && game.genre.toLowerCase().includes(q))
    );
  });

  state.filteredLibrary.sort((a, b) => {
    return (a.name || "").localeCompare(b.name || "", undefined, { sensitivity: "base", numeric: true });
  });

  const listEl = document.getElementById("game-list-container");
  listEl.innerHTML = "";

  if (state.filteredLibrary.length === 0) {
    const empty = document.createElement("div");
    empty.className = "library-empty-state";
    const isTotalEmpty = state.library.length === 0;
    empty.innerHTML = `
      <div class="empty-icon">
        <svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/>
          <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>
        </svg>
      </div>
      <div class="empty-title">${isTotalEmpty ? "No ROMs Loaded" : "No Matches Found"}</div>
      <div class="empty-desc">${isTotalEmpty ? "Your library is empty. Scan a folder containing Super Nintendo games (.sfc, .smc) or open a ROM to begin." : "No games match your search query or selected filter."}</div>
      ${isTotalEmpty ? '<button type="button" class="btn-empty-action" id="btn-empty-scan">Scan ROM Folder</button>' : ''}
    `;
    const btn = empty.querySelector("#btn-empty-scan");
    if (btn) {
      btn.addEventListener("click", () => handleMenuAction("scan-folder"));
    }
    listEl.appendChild(empty);
  } else {
    for (const game of state.filteredLibrary) {
      const item = document.createElement("div");
      item.className = "game-list-item" + (game.id === state.selectedGameId ? " selected" : "");
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", game.id === state.selectedGameId ? "true" : "false");
      item.dataset.gameId = game.id;

      const mainDiv = document.createElement("div");
      mainDiv.className = "game-item-main";

      const titleSpan = document.createElement("div");
      titleSpan.className = "game-item-title";
      titleSpan.textContent = game.name;

      const subSpan = document.createElement("div");
      subSpan.className = "game-item-sub";
      subSpan.textContent = `${game.region || "USA"} • ${game.genre || "Action"}`;

      mainDiv.appendChild(titleSpan);
      mainDiv.appendChild(subSpan);

      const rightDiv = document.createElement("div");
      rightDiv.className = "game-item-right";

      const yearSpan = document.createElement("span");
      yearSpan.className = "game-year-tag";
      yearSpan.textContent = game.year || "SNES";
      rightDiv.appendChild(yearSpan);

      if (game.hasLocalRom) {
        const dot = document.createElement("span");
        dot.className = "rom-ready-dot";
        dot.title = "Local ROM file ready to play";
        rightDiv.appendChild(dot);
      }

      item.appendChild(mainDiv);
      item.appendChild(rightDiv);

      item.addEventListener("click", () => selectGame(game.id));
      item.addEventListener("dblclick", () => {
        selectGame(game.id);
        ipc.send("launchGame", { gameId: game.id });
      });

      listEl.appendChild(item);
    }
  }

  document.getElementById("library-count-text").textContent = `${state.filteredLibrary.length} of ${state.library.length} games`;

  if (!state.selectedGameId && state.filteredLibrary.length > 0) {
    selectGame(state.filteredLibrary[0].id);
  } else if (state.library.length === 0 || !state.selectedGameId) {
    clearSelectedGameDetail();
  } else {
    renderSelectedGameDetail();
  }
}

function selectGame(gameId) {
  state.selectedGameId = gameId;
  document.querySelectorAll(".game-list-item").forEach((el) => {
    const selected = el.dataset.gameId === gameId;
    el.classList.toggle("selected", selected);
    el.setAttribute("aria-selected", selected ? "true" : "false");
  });
  renderSelectedGameDetail();
  ipc.send("selectGame", { gameId });
}

function clearSelectedGameDetail() {
  state.selectedGameId = null;
  document.getElementById("detail-title").textContent = "No Game Selected";
  document.getElementById("detail-nointro").textContent = "Super Nintendo Entertainment System";
  document.getElementById("detail-year").textContent = "—";
  document.getElementById("detail-description").textContent =
    "Your library is currently empty. Use the top menu 'File > Scan ROM Folder...' or 'Open ROM...' to load your Super Nintendo collection.";

  document.getElementById("detail-region").textContent = "—";
  document.getElementById("detail-hardware").textContent = "—";

  const statusBadge = document.getElementById("detail-rom-status");
  statusBadge.textContent = "Empty";
  statusBadge.classList.remove("ready");

  const playLabel = document.getElementById("btn-play-label");
  playLabel.textContent = "Open ROM...";

  document.getElementById("detail-developer").textContent = "—";
  document.getElementById("detail-publisher").textContent = "—";
  document.getElementById("detail-genre").textContent = "—";
  document.getElementById("detail-players").textContent = "—";
  document.getElementById("detail-serial").textContent = "—";
  document.getElementById("detail-crc32").textContent = "—";

  const imgEl = document.getElementById("detail-boxart-img");
  const placeholderEl = document.getElementById("detail-boxart-placeholder");
  imgEl.hidden = true;
  placeholderEl.hidden = false;
  document.getElementById("placeholder-cart-title").textContent = "SUPER NINTENDO";
  document.getElementById("placeholder-status-text").textContent = "NO ROM LOADED";
  const regionEl = document.getElementById("placeholder-box-region");
  if (regionEl) regionEl.textContent = "SNES";
}

function renderSelectedGameDetail() {
  const game = state.library.find((g) => g.id === state.selectedGameId);
  if (!game) {
    clearSelectedGameDetail();
    return;
  }

  document.getElementById("detail-title").textContent = game.name || "Unknown Game";
  document.getElementById("detail-nointro").textContent = game.noIntroName || game.name;
  document.getElementById("detail-year").textContent = game.year || "Unknown";
  document.getElementById("detail-description").textContent =
    game.description || "No description available for this title.";

  document.getElementById("detail-region").textContent = game.region || "USA";
  document.getElementById("detail-hardware").textContent = game.hardware || "Standard";

  const statusBadge = document.getElementById("detail-rom-status");
  const playLabel = document.getElementById("btn-play-label");
  if (game.hasLocalRom) {
    statusBadge.textContent = "ROM Ready";
    statusBadge.classList.add("ready");
    playLabel.textContent = "Play Now";
  } else {
    statusBadge.textContent = "Catalog Entry";
    statusBadge.classList.remove("ready");
    playLabel.textContent = "Locate ROM & Play...";
  }

  document.getElementById("detail-developer").textContent = game.developer || "Nintendo";
  document.getElementById("detail-publisher").textContent = game.publisher || "Nintendo";
  document.getElementById("detail-genre").textContent = game.genre || "Action";
  document.getElementById("detail-players").textContent = game.players || "1";
  document.getElementById("detail-serial").textContent = game.serial || "SNS-USA";
  document.getElementById("detail-crc32").textContent = game.crc32 || "—";

  const imgEl = document.getElementById("detail-boxart-img");
  const placeholderEl = document.getElementById("detail-boxart-placeholder");
  document.getElementById("placeholder-cart-title").textContent = game.name;
  document.getElementById("placeholder-status-text").textContent = "OFFICIAL SEAL OF QUALITY";
  const regionEl = document.getElementById("placeholder-box-region");
  if (regionEl) regionEl.textContent = game.region || "SNES";

  if (game.boxartUrl) {
    imgEl.src = game.boxartUrl;
    imgEl.hidden = false;
    placeholderEl.hidden = true;
    imgEl.onerror = () => {
      imgEl.hidden = true;
      placeholderEl.hidden = false;
    };
  } else {
    imgEl.hidden = true;
    placeholderEl.hidden = false;
  }
}

function syncWorkspaceMode() {
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

  if (document.startViewTransition && libView.hidden === isRunning) {
    document.startViewTransition(applySwitch);
  } else {
    applySwitch();
  }
}

// ============================================================================
// MODALS: CONTROLS (4 PLAYERS), GRAPHICS, EMULATION, DATABASE
// ============================================================================
function openControlsModal() {
  closeAllMenus();
  const dlg = document.getElementById("dialog-controls");
  ipc.send("refreshDevices");
  renderControlsModalPlayer(state.editingPlayerTab);
  dlg.showModal();
  updateModalState();
}

function renderControlsModalPlayer(playerNum) {
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

function highlightSvgButton(btnName, highlight) {
  const svgEl = document.querySelector(`[data-svg-btn="${btnName}"]`);
  if (svgEl) {
    svgEl.classList.toggle("highlight", highlight);
  }
}

function startListeningForBind(btnName, rowEl, keyBtnEl) {
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
  }, 60);
}

function stopListeningForBind() {
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

function applyCapturedBinding(code) {
  if (!state.listeningButtonName) return;
  const playerCfg = state.controls.players.find((p) => p.playerIndex === state.editingPlayerTab);
  if (playerCfg) {
    if (!playerCfg.buttons) playerCfg.buttons = {};
    playerCfg.buttons[state.listeningButtonName] = code;
  }
  stopListeningForBind();
  renderControlsModalPlayer(state.editingPlayerTab);
}

function openGraphicsModal() {
  closeAllMenus();
  const gfx = state.settings.graphics;
  document.getElementById("gfx-aspect-select").value = gfx.aspectRatio || "4:3";
  document.getElementById("gfx-cpu-filter-select").value = gfx.cpuFilter || "none";
  document.getElementById("gfx-integer-check").checked = Boolean(gfx.integerScaling);
  document.getElementById("gfx-bilinear-check").checked = Boolean(gfx.bilinearFiltering);
  document.getElementById("gfx-crt-check").checked = Boolean(gfx.crtShader);
  document.getElementById("gfx-overscan-check").checked = Boolean(gfx.overscanCrop);
  document.getElementById("gfx-fps-check").checked = Boolean(gfx.showFps);
  document.getElementById("gfx-fullscreen-check").checked = Boolean(gfx.fullscreen);
  document.getElementById("dialog-graphics").showModal();
  updateModalState();
}

function openEmulationModal() {
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

function openDatabaseModal() {
  closeAllMenus();
  const dlg = document.getElementById("dialog-database");
  dlg.showModal();
  updateModalState();
  const q = document.getElementById("db-modal-search-input").value.trim();
  ipc.send("searchDatabase", { query: q });
}

// ============================================================================
// IPC EVENT LISTENERS
// ============================================================================
ipc.on("appState", (data) => {
  if (Array.isArray(data.library)) state.library = data.library;
  if (data.settings) state.settings = data.settings;
  if (data.controls) state.controls = data.controls;
  if (Array.isArray(data.devices)) state.devices = data.devices;
  if (data.emulation) state.emulation = data.emulation;

  syncMenuIndicators();
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
    syncMenuIndicators();
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
  const container = document.getElementById("db-modal-results");
  container.innerHTML = "";
  const results = data.results || [];
  if (results.length === 0) {
    container.innerHTML = `<p style="color: var(--text-secondary); padding: 12px;">No matching SNES games found.</p>`;
    return;
  }
  for (const item of results) {
    const row = document.createElement("div");
    row.className = "db-result-item";
    row.innerHTML = `
      <div>
        <strong>${item.name}</strong>
        <div style="font-size: 12px; color: var(--text-secondary);">
          ${item.year} • ${item.region} • ${item.developer || "Nintendo"} • ${item.genre}
        </div>
      </div>
    `;
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
});

ipc.on("toast", (data) => {
  if (data.message) showToast(data.message);
});

ipc.on("navLibrary", (data) => {
  if (state.emulation.running) return;

  // If currently capturing a button bind in Configure Controls, ignore ALL gamepad nav events
  if (state.listeningButtonName) {
    return;
  }

  // If any modal dialog is open (e.g. Configure Controls), do NOT navigate the game list or launch games!
  const openModal = document.querySelector("dialog.modal-dialog[open]");
  if (openModal) {
    if (data.action === "back") {
      closeTopLayer(openModal);
    }
    return;
  }

  const action = data.action;
  if (!state.filteredLibrary || state.filteredLibrary.length === 0) {
    if (action === "launch") {
      handleMenuAction("scan-folder");
    }
    return;
  }

  const currentIdx = state.filteredLibrary.findIndex((g) => g.id === state.selectedGameId);
  let nextIdx = currentIdx;

  if (action === "down") {
    nextIdx = currentIdx < state.filteredLibrary.length - 1 ? currentIdx + 1 : 0;
  } else if (action === "up") {
    nextIdx = currentIdx > 0 ? currentIdx - 1 : state.filteredLibrary.length - 1;
  } else if (action === "pageDown") {
    nextIdx = Math.min(state.filteredLibrary.length - 1, (currentIdx >= 0 ? currentIdx : 0) + 10);
  } else if (action === "pageUp") {
    nextIdx = Math.max(0, (currentIdx >= 0 ? currentIdx : 0) - 10);
  } else if (action === "launch") {
    if (state.selectedGameId) {
      ipc.send("launchGame", { gameId: state.selectedGameId });
    }
    return;
  } else if (action === "back") {
    document.querySelectorAll("dialog.modal-dialog[open]").forEach((dlg) => closeTopLayer(dlg));
    return;
  }

  if (nextIdx !== currentIdx && nextIdx >= 0 && nextIdx < state.filteredLibrary.length) {
    const nextGame = state.filteredLibrary[nextIdx];
    selectGame(nextGame.id);
    const itemEl = document.querySelector(`.game-list-item[data-game-id="${nextGame.id}"]`);
    if (itemEl) {
      itemEl.scrollIntoView({ block: "nearest", behavior: "smooth" });
    }
  }
});

// ============================================================================
// DOM & KEYBOARD LISTENERS
// ============================================================================
function initDomEvents() {
  initMenubar();

  // Setup light-dismiss & close buttons on all dialogs
  document.querySelectorAll("dialog.modal-dialog").forEach((dlg) => {
    setupDialogLightDismiss(dlg);
  });

  document.querySelectorAll("[data-close-dialog]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const targetId = btn.dataset.closeDialog;
      closeTopLayer(document.getElementById(targetId));
    });
  });

  // Search Input & Filter Chips
  document.getElementById("game-search-input").addEventListener("input", (e) => {
    state.searchQuery = e.target.value;
    filterAndRenderLibrary();
  });

  document.querySelectorAll(".filter-chip").forEach((chip) => {
    chip.addEventListener("click", () => {
      document.querySelectorAll(".filter-chip").forEach((c) => c.classList.remove("active"));
      chip.classList.add("active");
      state.activeFilter = chip.dataset.filter;
      filterAndRenderLibrary();
    });
  });

  // Play Button on Right Detail Pane
  document.getElementById("btn-play-selected").addEventListener("click", () => {
    if (state.selectedGameId) {
      ipc.send("launchGame", { gameId: state.selectedGameId });
    } else {
      ipc.send("openRomDialog", { autoStart: true });
    }
  });

  // Controls Modal Events
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

  // Graphics Modal Save
  document.getElementById("btn-save-graphics").addEventListener("click", () => {
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

  // Emulation Modal Save
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

  // Global Keyboard Shortcuts & Emulation Input Forwarding
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
      handleMenuAction("toggle-fullscreen");
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

// Boot Frontend View
initDomEvents();
ipc.send("initApp");

