// Snex9x v2 — Shared Frontend State & Controller Constants

export const SNES_BUTTON_ORDER = [
  "Up", "Down", "Left", "Right",
  "A", "B", "X", "Y",
  "L", "R", "Start", "Select"
];

export const state = {
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
