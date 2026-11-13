// Snex9x v2 — Dual Light / Dark Theme Synchronization & Cycling Module

const THEME_STORAGE_KEY = "color-scheme";
const colorSchemeMedia = window.matchMedia("(prefers-color-scheme: dark)");

/**
 * Evaluates the OS preferred color scheme media query.
 * @returns {"dark" | "light"}
 */
export function getSystemTheme() {
  return colorSchemeMedia.matches ? "dark" : "light";
}

/**
 * Reads any persisted explicit theme override from localStorage.
 * @returns {"light" | "dark" | null}
 */
export function getStoredTheme() {
  try {
    const val = localStorage.getItem(THEME_STORAGE_KEY);
    return val === "light" || val === "dark" ? val : null;
  } catch {
    return null;
  }
}

/**
 * Persists a light, dark, or system theme choice, updates <meta name="color-scheme">,
 * and synchronizes all visible theme indicators.
 * @param {"light" | "dark" | "system"} mode
 */
export function applyThemeMode(mode) {
  const meta = document.querySelector('meta[name="color-scheme"]');
  if (mode === "light" || mode === "dark") {
    try {
      localStorage.setItem(THEME_STORAGE_KEY, mode);
    } catch {}
    if (meta) meta.content = mode;
  } else {
    try {
      localStorage.removeItem(THEME_STORAGE_KEY);
    } catch {}
    if (meta) meta.content = "light dark";
  }
  syncThemeControls();
}

/**
 * Cycles between SNES Light and Dark themes; clears the stored override if toggling
 * returns to the current system preference so automatic OS tracking resumes.
 */
export function toggleTheme() {
  const effective = getStoredTheme() ?? getSystemTheme();
  const target = effective === "dark" ? "light" : "dark";
  applyThemeMode(target === getSystemTheme() ? "system" : target);
}

/**
 * Synchronizes checked radio states in the Graphics menu, the Graphics modal selector,
 * and the menubar toggle button tooltip.
 */
export function syncThemeControls() {
  const stored = getStoredTheme();
  const currentSetting = stored ?? "system";
  const effective = stored ?? getSystemTheme();

  document.querySelectorAll("[data-setting-theme]").forEach((btn) => {
    const isSelected = btn.dataset.settingTheme === currentSetting;
    btn.classList.toggle("selected", isSelected);
    btn.setAttribute("aria-checked", isSelected ? "true" : "false");
  });

  const themeSelect = document.getElementById("gfx-theme-select");
  if (themeSelect) {
    themeSelect.value = currentSetting;
  }

  const toggleBtn = document.getElementById("btn-theme-toggle");
  if (toggleBtn) {
    toggleBtn.title = effective === "dark" ? "Switch to SNES Light Theme" : "Switch to SNES Dark Theme";
  }
}

/**
 * Registers event listeners for the theme toggle button, Graphics modal theme select,
 * OS color-scheme changes, and cross-window storage updates.
 */
export function initThemeEvents() {
  document.getElementById("btn-theme-toggle")?.addEventListener("click", () => {
    toggleTheme();
  });

  document.getElementById("gfx-theme-select")?.addEventListener("change", (e) => {
    applyThemeMode(e.target.value);
  });

  colorSchemeMedia.addEventListener("change", () => {
    syncThemeControls();
  });

  window.addEventListener("storage", (e) => {
    if (e.key === THEME_STORAGE_KEY) {
      const meta = document.querySelector('meta[name="color-scheme"]');
      const next = getStoredTheme();
      if (meta) meta.content = next ?? "light dark";
      syncThemeControls();
    }
  });

  syncThemeControls();
}
