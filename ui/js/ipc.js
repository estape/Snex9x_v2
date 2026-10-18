// Snex9x v2 — IPC Bridge (Photino.Native / WebView2 <-> Frontend View)

class IpcBridge {
  constructor() {
    this.handlers = new Map();
    this.sharedBufferListeners = [];
    this.sharedVideoBuffer = null;

    if (window.external && typeof window.external.receiveMessage === "function") {
      window.external.receiveMessage((rawMessage) => {
        this._dispatchMessage(rawMessage);
      });
    } else if (window.chrome && window.chrome.webview) {
      window.chrome.webview.addEventListener("message", (event) => {
        this._dispatchMessage(event.data);
      });
    }

    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.addEventListener("sharedbufferreceived", (event) => {
        try {
          const buf = event.getBuffer();
          this.sharedVideoBuffer = buf;
          console.log("[IPC] Shared video buffer successfully attached, byteLength:", buf ? buf.byteLength : 0);
          for (const cb of this.sharedBufferListeners) {
            cb(buf);
          }
        } catch (err) {
          console.error("[IPC] Failed to attach shared video buffer:", err);
        }
      });
    }
  }

  _dispatchMessage(rawMessage) {
    try {
      const data = typeof rawMessage === "string" ? JSON.parse(rawMessage) : rawMessage;
      if (!data || !data.type) return;
      const list = this.handlers.get(data.type);
      if (list) {
        for (const fn of list) {
          fn(data);
        }
      }
    } catch (err) {
      console.error("[IPC] Error parsing backend message:", err);
    }
  }

  on(type, callback) {
    if (!this.handlers.has(type)) {
      this.handlers.set(type, []);
    }
    this.handlers.get(type).push(callback);
  }

  onSharedVideoBuffer(callback) {
    this.sharedBufferListeners.push(callback);
    if (this.sharedVideoBuffer) {
      callback(this.sharedVideoBuffer);
    }
  }

  send(action, payload = {}) {
    const message = JSON.stringify({ action, ...payload });
    if (window.external && typeof window.external.sendMessage === "function") {
      window.external.sendMessage(message);
    } else if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(message);
    }
  }
}

export const ipc = new IpcBridge();

