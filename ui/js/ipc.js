// Snex9x v2 — IPC Bridge (Photino.Native / WebView2 <-> Frontend View)

const IPC_LOG_PREFIX = "[IPC]";

/**
 * Bidirectional messaging and shared video buffer bridge between WebView2/Photino
 * and the C++ AppController host.
 */
class IpcBridge {
  /**
   * Initializes handler registries and subscribes to Photino/WebView2 message
   * and shared memory buffer events.
   */
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
          console.log(`${IPC_LOG_PREFIX} Shared video buffer successfully attached, byteLength:`, buf ? buf.byteLength : 0);
          for (const cb of this.sharedBufferListeners) {
            cb(buf);
          }
        } catch (err) {
          console.error(`${IPC_LOG_PREFIX} Failed to attach shared video buffer:`, err);
        }
      });
    }
  }

  /**
   * Parses incoming JSON messages from the host and invokes subscribed callbacks for data.type.
   * @param {string | Record<string, any>} rawMessage
   */
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
      console.error(`${IPC_LOG_PREFIX} Error parsing backend message:`, err);
    }
  }

  /**
   * Subscribes a listener callback to a named backend IPC event type.
   * @param {string} type
   * @param {(data: Record<string, any>) => void} callback
   */
  on(type, callback) {
    if (!this.handlers.has(type)) {
      this.handlers.set(type, []);
    }
    this.handlers.get(type).push(callback);
  }

  /**
   * Registers a callback for the native shared video framebuffer and replays immediately
   * if the shared buffer has already been attached.
   * @param {(buf: ArrayBuffer) => void} callback
   */
  onSharedVideoBuffer(callback) {
    this.sharedBufferListeners.push(callback);
    if (this.sharedVideoBuffer) {
      callback(this.sharedVideoBuffer);
    }
  }

  /**
   * Serializes { action, ...payload } as JSON and posts it to the native host controller.
   * @param {string} action
   * @param {Record<string, any>} [payload]
   */
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
