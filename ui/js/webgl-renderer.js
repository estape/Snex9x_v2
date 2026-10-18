// Snex9x v2 — WebGL2 Zero-Copy SNES Video Renderer with CRT Scanline & Curvature Shader

const VERTEX_SHADER_SRC = `#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
  v_uv = a_uv;
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
`;

const FRAGMENT_SHADER_SRC = `#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_texSize;
uniform int u_enableCrt;
out vec4 outColor;

vec2 crtCurve(vec2 uv) {
  uv = uv * 2.0 - 1.0;
  vec2 offset = abs(uv.yx) / vec2(6.5, 5.5);
  uv = uv + uv * offset * offset;
  uv = uv * 0.5 + 0.5;
  return uv;
}

void main() {
  if (u_enableCrt == 0) {
    outColor = texture(u_tex, v_uv);
    return;
  }

  vec2 uv = crtCurve(v_uv);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  // Slight RGB phosphor subpixel convergence offset
  float chromaShift = 0.0009;
  vec3 col;
  col.r = texture(u_tex, vec2(uv.x + chromaShift, uv.y)).r;
  col.g = texture(u_tex, uv).g;
  col.b = texture(u_tex, vec2(uv.x - chromaShift, uv.y)).b;

  // Horizontal CRT scanlines synced to internal vertical resolution
  float scanline = sin(uv.y * u_texSize.y * 3.14159265) * 0.5 + 0.5;
  float scanFactor = mix(0.76, 1.06, pow(scanline, 0.65));
  col *= scanFactor;

  // Subtle Trinitron aperture grille vertical modulation
  float grille = sin(uv.x * u_texSize.x * 3.14159265 * 2.0) * 0.04 + 0.98;
  col *= grille;

  // Corner vignette
  vec2 vigUv = uv * (1.0 - uv.yx);
  float vig = clamp(pow(vigUv.x * vigUv.y * 16.0, 0.18), 0.0, 1.0);
  col *= vig;

  outColor = vec4(col, 1.0);
}
`;

export class WebGLRenderer {
  constructor(canvas, stageEl, onMetricsUpdate) {
    this.canvas = canvas;
    this.stageEl = stageEl;
    this.onMetricsUpdate = onMetricsUpdate;

    this.sharedBuffer = null;
    this.lastFrameCounter = 0;
    this.frameWidth = 256;
    this.frameHeight = 224;
    this.running = false;
    this.fetchingFallback = false;

    this.settings = {
      aspectRatio: "4:3",
      integerScaling: false,
      bilinearFiltering: false,
      crtShader: false,
      showFps: false
    };

    this.gl = null;
    this.ctx2d = null;
    this.program = null;
    this.texture = null;
    this.vao = null;
    this.uTexSizeLoc = null;
    this.uEnableCrtLoc = null;

    this._initContext();

    window.addEventListener("resize", () => this.updateViewportLayout());

    if (window.ResizeObserver && this.stageEl) {
      this.resizeObserver = new ResizeObserver(() => {
        this.updateViewportLayout();
      });
      this.resizeObserver.observe(this.stageEl);
    }
  }

  _initContext() {
    try {
      const gl = this.canvas.getContext("webgl2", {
        alpha: false,
        antialias: false,
        depth: false,
        stencil: false,
        preserveDrawingBuffer: false,
        powerPreference: "default"
      });

      if (!gl) {
        console.warn("[Renderer] WebGL2 not available, falling back to Canvas2D context.");
        this.ctx2d = this.canvas.getContext("2d");
        return;
      }

      this.gl = gl;
      const vs = this._compileShader(gl.VERTEX_SHADER, VERTEX_SHADER_SRC);
      const fs = this._compileShader(gl.FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);
      if (!vs || !fs) {
        this.gl = null;
        this.ctx2d = this.canvas.getContext("2d");
        return;
      }

      const prog = gl.createProgram();
      gl.attachShader(prog, vs);
      gl.attachShader(prog, fs);
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error("[WebGL2] Program link error:", gl.getProgramInfoLog(prog));
        this.gl = null;
        this.ctx2d = this.canvas.getContext("2d");
        return;
      }
      this.program = prog;
      gl.useProgram(prog);

      const uTexLoc = gl.getUniformLocation(prog, "u_tex");
      if (uTexLoc !== null) {
        gl.uniform1i(uTexLoc, 0);
      }

      this.uTexSizeLoc = gl.getUniformLocation(prog, "u_texSize");
      this.uEnableCrtLoc = gl.getUniformLocation(prog, "u_enableCrt");

      // Fullscreen quad (pos.x, pos.y, uv.x, uv.y) with flipped V for top-left origin
      const quadData = new Float32Array([
        -1, -1, 0, 1,
         1, -1, 1, 1,
        -1,  1, 0, 0,
         1,  1, 1, 0
      ]);

      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      const vbo = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
      gl.bufferData(gl.ARRAY_BUFFER, quadData, gl.STATIC_DRAW);

      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
      this.vao = vao;

      const tex = gl.createTexture();
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 256, 224, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      this.texture = tex;
    } catch (err) {
      console.error("[Renderer] Exception initializing WebGL2 context, falling back to Canvas2D:", err);
      this.gl = null;
      this.ctx2d = this.canvas.getContext("2d");
    }
  }

  _compileShader(type, source) {
    const gl = this.gl;
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.error("[WebGL2] Shader compile error:", gl.getShaderInfoLog(shader));
      gl.deleteShader(shader);
      return null;
    }
    return shader;
  }

  attachSharedBuffer(arrayBuffer) {
    this.sharedBuffer = arrayBuffer;
    this.lastFrameCounter = 0;
  }

  applySettings(gfxSettings) {
    if (!gfxSettings) return;
    this.settings = { ...this.settings, ...gfxSettings };
    this.canvas.style.imageRendering = this.settings.bilinearFiltering ? "auto" : "pixelated";

    if (this.gl && this.texture) {
      const gl = this.gl;
      gl.bindTexture(gl.TEXTURE_2D, this.texture);
      const filter = this.settings.bilinearFiltering ? gl.LINEAR : gl.NEAREST;
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    }

    this.updateViewportLayout();
  }

  updateViewportLayout() {
    if (!this.stageEl || !this.canvas) return;
    const rect = this.stageEl.getBoundingClientRect();
    const maxW = Math.max(rect.width - 24, 256);
    const maxH = Math.max(rect.height - 24, 224);

    if (this.settings.aspectRatio === "stretch") {
      this.canvas.style.width = `${Math.floor(maxW)}px`;
      this.canvas.style.height = `${Math.floor(maxH)}px`;
      return;
    }

    const targetAspect = this.settings.aspectRatio === "8:7" ? (8.0 / 7.0) : (4.0 / 3.0);

    if (this.settings.integerScaling) {
      const baseH = 224;
      const baseW = Math.round(baseH * targetAspect);
      const scale = Math.max(1, Math.floor(Math.min(maxW / baseW, maxH / baseH)));
      this.canvas.style.width = `${baseW * scale}px`;
      this.canvas.style.height = `${baseH * scale}px`;
    } else {
      let drawH = maxH;
      let drawW = drawH * targetAspect;
      if (drawW > maxW) {
        drawW = maxW;
        drawH = drawW / targetAspect;
      }
      this.canvas.style.width = `${Math.floor(drawW)}px`;
      this.canvas.style.height = `${Math.floor(drawH)}px`;
    }
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.updateViewportLayout();
    const loop = () => {
      if (!this.running) return;
      this._renderTick();
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }

  stop() {
    this.running = false;
  }

  _renderTick() {
    if (this.sharedBuffer && this.sharedBuffer.byteLength >= 16) {
      this._consumeFrameBuffer(this.sharedBuffer);
    } else if (!this.fetchingFallback) {
      this.fetchingFallback = true;
      // Use same-origin relative URL to avoid Chromium CORS blocking across custom scheme hosts
      fetch("api/frame/latest.bin")
        .then((r) => (r.ok ? r.arrayBuffer() : null))
        .then((buf) => {
          if (buf && buf.byteLength >= 16) {
            this._consumeFrameBuffer(buf);
          }
        })
        .catch((err) => {
          // Silent fallback catch
        })
        .finally(() => {
          this.fetchingFallback = false;
        });
    }
  }

  _consumeFrameBuffer(buffer) {
    const header = new Uint32Array(buffer, 0, 4);
    const frameCounter = header[0];
    const width = header[1];
    const height = header[2];
    const flags = header[3];

    if (frameCounter === 0 || frameCounter === this.lastFrameCounter) {
      return;
    }
    if (width < 16 || width > 1024 || height < 16 || height > 960) {
      return;
    }

    const byteCount = width * height * 4;
    if (buffer.byteLength < 16 + byteCount) {
      return;
    }

    this.lastFrameCounter = frameCounter;
    const sizeChanged = width !== this.frameWidth || height !== this.frameHeight;
    this.frameWidth = width;
    this.frameHeight = height;

    if (sizeChanged) {
      this.canvas.width = width;
      this.canvas.height = height;
      this.updateViewportLayout();
    }

    const pixels = new Uint8Array(buffer, 16, byteCount);
    let webglRendered = false;

    if (this.gl && this.program && this.texture) {
      try {
        const gl = this.gl;
        gl.viewport(0, 0, width, height);
        gl.useProgram(this.program);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.texture);

        if (sizeChanged) {
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        } else {
          // Correct 9-argument WebGL2 signature: target, level, xoffset, yoffset, width, height, format, type, pixels
          gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        }

        gl.uniform2f(this.uTexSizeLoc, width, height);
        gl.uniform1i(this.uEnableCrtLoc, this.settings.crtShader ? 1 : 0);

        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        webglRendered = true;
      } catch (err) {
        console.error("[Renderer] WebGL2 draw error, switching to Canvas2D software failover:", err);
        this.gl = null;
        this.ctx2d = this.canvas.getContext("2d");
      }
    }

    if (!webglRendered) {
      if (!this.ctx2d) {
        this.ctx2d = this.canvas.getContext("2d");
      }
      if (this.ctx2d) {
        const clamped = new Uint8ClampedArray(buffer, 16, byteCount);
        const imgData = new ImageData(clamped, width, height);
        this.ctx2d.putImageData(imgData, 0, 0);
      }
    }

    if (this.onMetricsUpdate) {
      const fps = ((flags >>> 16) & 0xffff) / 10.0;
      this.onMetricsUpdate(fps, width, height);
    }
  }
}

