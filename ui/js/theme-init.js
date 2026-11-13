// Snex9x v2 — Synchronous Pre-Paint Color Scheme Initializer

const THEME_STORAGE_KEY = "color-scheme";
const VALID_THEMES = new Set(["light", "dark"]);

/**
 * Reads any saved color-scheme preference from localStorage and updates
 * <meta name="color-scheme"> synchronously before stylesheets paint.
 */
function applyInitialColorScheme() {
  try {
    const savedScheme = localStorage.getItem(THEME_STORAGE_KEY);
    const meta = document.querySelector('meta[name="color-scheme"]');
    if (meta && VALID_THEMES.has(savedScheme)) {
      meta.content = savedScheme;
    }
  } catch {}
}

applyInitialColorScheme();
