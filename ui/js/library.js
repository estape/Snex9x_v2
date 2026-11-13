// Snex9x v2 — Game Library List, Search/Filter & Right-Pane Showcase Module

import { ipc } from "./ipc.js";
import { state } from "./state.js";
import { closeTopLayer } from "./modals.js";

const PAGE_STEP_SIZE = 10;
let scanFolderHandler = () => ipc.send("scanRomFolderDialog");

/**
 * Filters and sorts state.library by active filter chip and search text, renders
 * #game-list-container options (or the empty-state prompt), and refreshes selection details.
 */
export function filterAndRenderLibrary() {
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
      ${isTotalEmpty ? '<button type="button" class="btn-empty-action" id="btn-empty-scan">Scan ROM Folder</button>' : ""}
    `;
    const btn = empty.querySelector("#btn-empty-scan");
    if (btn) {
      btn.addEventListener("click", () => scanFolderHandler());
    }
    listEl.appendChild(empty);
  } else {
    state.filteredLibrary.forEach((game, idx) => {
      const isSelected = game.id === state.selectedGameId;
      const item = document.createElement("div");
      item.className = "game-list-item" + (isSelected ? " selected" : "");
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", isSelected ? "true" : "false");
      item.tabIndex = isSelected || (!state.selectedGameId && idx === 0) ? 0 : -1;
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
    });
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

/**
 * Marks a game entry as active, updates roving tabindex & aria-selected states,
 * renders the right detail panel, and notifies the backend.
 * @param {string} gameId
 * @param {{ focusItem?: boolean }} [options]
 */
export function selectGame(gameId, { focusItem = false } = {}) {
  state.selectedGameId = gameId;
  document.querySelectorAll(".game-list-item").forEach((el) => {
    const selected = el.dataset.gameId === gameId;
    el.classList.toggle("selected", selected);
    el.setAttribute("aria-selected", selected ? "true" : "false");
    el.tabIndex = selected ? 0 : -1;
    if (selected && focusItem) {
      el.focus();
      el.scrollIntoView({ block: "nearest" });
    }
  });
  renderSelectedGameDetail();
  ipc.send("selectGame", { gameId });
}

/**
 * Resets the right detail pane metadata, badges, and cartridge placeholder when no game is selected.
 */
export function clearSelectedGameDetail() {
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

/**
 * Updates the right-pane boxart showcase, placeholder fallback, metadata cards, and Play CTA.
 */
export function renderSelectedGameDetail() {
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

/**
 * Handles SDL3 gamepad navigation actions (up, down, pageUp, pageDown, launch, back)
 * inside the library view while respecting open modals and active rebind capture.
 * @param {{ action?: string }} data
 */
export function handleLibraryGamepadNav(data) {
  if (state.emulation.running) return;
  if (state.listeningButtonName) return;

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
      scanFolderHandler();
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
    nextIdx = Math.min(state.filteredLibrary.length - 1, (currentIdx >= 0 ? currentIdx : 0) + PAGE_STEP_SIZE);
  } else if (action === "pageUp") {
    nextIdx = Math.max(0, (currentIdx >= 0 ? currentIdx : 0) - PAGE_STEP_SIZE);
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
}

/**
 * Registers library search input, filter chip toggles, listbox keyboard navigation,
 * and Play Selected CTA listeners.
 * @param {{ onScanFolder?: () => void }} [options]
 */
export function initLibraryEvents({ onScanFolder } = {}) {
  if (typeof onScanFolder === "function") {
    scanFolderHandler = onScanFolder;
  }

  document.getElementById("game-search-input").addEventListener("input", (e) => {
    state.searchQuery = e.target.value;
    filterAndRenderLibrary();
  });

  const gameListEl = document.getElementById("game-list-container");
  if (gameListEl) {
    gameListEl.addEventListener("keydown", (e) => {
      if (state.filteredLibrary.length === 0) return;
      const curIdx = state.filteredLibrary.findIndex((g) => g.id === state.selectedGameId);
      let nextIdx = -1;
      if (e.key === "ArrowDown") {
        nextIdx = curIdx < 0 ? 0 : Math.min(state.filteredLibrary.length - 1, curIdx + 1);
      } else if (e.key === "ArrowUp") {
        nextIdx = curIdx <= 0 ? 0 : curIdx - 1;
      } else if (e.key === "Home") {
        nextIdx = 0;
      } else if (e.key === "End") {
        nextIdx = state.filteredLibrary.length - 1;
      } else if (e.key === "Enter" && state.selectedGameId) {
        e.preventDefault();
        ipc.send("launchGame", { gameId: state.selectedGameId });
        return;
      }
      if (nextIdx >= 0) {
        e.preventDefault();
        selectGame(state.filteredLibrary[nextIdx].id, { focusItem: true });
      }
    });
  }

  document.querySelectorAll(".filter-chip").forEach((chip) => {
    chip.addEventListener("click", () => {
      document.querySelectorAll(".filter-chip").forEach((c) => c.classList.remove("active"));
      chip.classList.add("active");
      state.activeFilter = chip.dataset.filter;
      filterAndRenderLibrary();
    });
  });

  document.getElementById("btn-play-selected").addEventListener("click", () => {
    if (state.selectedGameId) {
      ipc.send("launchGame", { gameId: state.selectedGameId });
    } else {
      ipc.send("openRomDialog", { autoStart: true });
    }
  });
}
