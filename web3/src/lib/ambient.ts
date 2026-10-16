// Ambient light: a slow mesh gradient in the current photo's colours behind the card. WebGL2 at a
// quarter of the screen's resolution (it is all blur anyway), ~20 frames per second while drifting,
// paused while the page is hidden; a CSS gradient stands in where WebGL2 is missing.

type RGB = [number, number, number];

const VERT = `#version 300 es
in vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }`;

const FRAG = `#version 300 es
precision mediump float;
uniform vec2 res; uniform float t; uniform vec3 c[4];
out vec4 o;
void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 q[4];
  q[0] = vec2(0.25 + 0.10 * sin(t * 0.21), 0.30 + 0.08 * cos(t * 0.17));
  q[1] = vec2(0.78 + 0.08 * cos(t * 0.19), 0.28 + 0.10 * sin(t * 0.23));
  q[2] = vec2(0.30 + 0.09 * cos(t * 0.15), 0.80 + 0.07 * sin(t * 0.20));
  q[3] = vec2(0.75 + 0.10 * sin(t * 0.18), 0.76 + 0.09 * cos(t * 0.22));
  vec3 col = vec3(0.0); float wsum = 0.0;
  for (int i = 0; i < 4; i++) {
    float d = distance(uv, q[i]);
    float w = 1.0 / (0.02 + d * d * 6.0);
    col += c[i] * w; wsum += w;
  }
  col /= wsum;
  float v = smoothstep(1.15, 0.25, distance(uv, vec2(0.5)));  // vignette
  o = vec4(col * (0.45 + 0.55 * v), 1.0);
}`;

export class Ambient {
  private gl: WebGL2RenderingContext | null;
  private prog: WebGLProgram | null = null;
  private from: RGB[] = Array(4).fill([0.06, 0.06, 0.07]);
  private to: RGB[] = this.from;
  private fadeStart = 0;
  private raf = 0;
  private last = 0;
  private uC: WebGLUniformLocation | null = null;
  private uT: WebGLUniformLocation | null = null;
  private uRes: WebGLUniformLocation | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    this.gl = canvas.getContext('webgl2', { antialias: false, alpha: false, powerPreference: 'low-power' });
    if (this.gl) this.setup(this.gl);
    else canvas.classList.add('no-gl');
    this.resize();
    addEventListener('resize', this.resize);
    document.addEventListener('visibilitychange', this.visibility);
    this.start();
  }

  private setup(gl: WebGL2RenderingContext) {
    const sh = (type: number, src: string) => {
      const s = gl.createShader(type)!;
      gl.shaderSource(s, src);
      gl.compileShader(s);
      return s;
    };
    const p = gl.createProgram()!;
    gl.attachShader(p, sh(gl.VERTEX_SHADER, VERT));
    gl.attachShader(p, sh(gl.FRAGMENT_SHADER, FRAG));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      this.gl = null;
      this.canvas.classList.add('no-gl');
      return;
    }
    gl.useProgram(p);
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(p, 'p');
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    this.uC = gl.getUniformLocation(p, 'c');
    this.uT = gl.getUniformLocation(p, 't');
    this.uRes = gl.getUniformLocation(p, 'res');
    this.prog = p;
  }

  private resize = () => {
    this.canvas.width = Math.max(1, Math.round(innerWidth / 4));
    this.canvas.height = Math.max(1, Math.round(innerHeight / 4));
    this.gl?.viewport(0, 0, this.canvas.width, this.canvas.height);
  };

  private visibility = () => (document.hidden ? this.stop() : this.start());
  private held = false;

  /** Rest while something opaque covers the light (true), or light up again (false). */
  hold(on: boolean) {
    this.held = on;
    if (on) this.stop();
    else if (!document.hidden) this.start();
  }

  /** New colours (0..1 RGB), crossfaded over 400 ms. */
  setColors(colors: RGB[]) {
    const now = performance.now();
    this.from = this.current(now);
    this.to = colors.slice(0, 4);
    while (this.to.length < 4) this.to.push(this.to[this.to.length - 1] ?? [0.06, 0.06, 0.07]);
    this.fadeStart = now;
    if (!this.gl) this.canvas.style.background = cssGradient(this.to);
  }

  private current(now: number): RGB[] {
    const k = Math.min(1, (now - this.fadeStart) / 400);
    return this.to.map((c, i) => c.map((v, j) => this.from[i][j] + (v - this.from[i][j]) * k) as RGB);
  }

  private frame = (now: number) => {
    this.raf = requestAnimationFrame(this.frame);
    const fading = now - this.fadeStart < 420;
    if (!fading && now - this.last < 50) return; // ~20 fps is plenty for a slow drift
    this.last = now;
    const gl = this.gl;
    if (!gl || !this.prog) return;
    gl.uniform2f(this.uRes, this.canvas.width, this.canvas.height);
    gl.uniform1f(this.uT, now / 1000);
    gl.uniform3fv(this.uC, this.current(now).flat());
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  };

  start() {
    if (!this.raf && this.gl && !this.held) this.raf = requestAnimationFrame(this.frame);
  }

  stop() {
    cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  /** For good: the view is gone, so it no longer follows the window or the page's visibility. */
  dispose() {
    this.stop();
    removeEventListener('resize', this.resize);
    document.removeEventListener('visibilitychange', this.visibility);
  }
}

function cssGradient(c: RGB[]): string {
  const css = (x: RGB) => `rgb(${x.map((v) => Math.round(v * 255)).join(' ')})`;
  return `radial-gradient(at 25% 30%, ${css(c[0])}, transparent 60%), radial-gradient(at 78% 28%, ${css(c[1])}, transparent 60%),
    radial-gradient(at 30% 80%, ${css(c[2])}, transparent 60%), radial-gradient(at 75% 76%, ${css(c[3])}, #0e0f11 70%)`;
}

/** Four colours of a picture (its quadrants), deepened so the light stays behind the photo. */
export async function paletteOf(url: string): Promise<RGB[]> {
  const img = new Image();
  img.src = url;
  await img.decode();
  const cv = new OffscreenCanvas(8, 8);
  const ctx = cv.getContext('2d')!;
  ctx.drawImage(img, 0, 0, 8, 8);
  const d = ctx.getImageData(0, 0, 8, 8).data;
  const quad = (x0: number, y0: number): RGB => {
    let r = 0, g = 0, b = 0;
    for (let y = y0; y < y0 + 4; y++)
      for (let x = x0; x < x0 + 4; x++) {
        const i = (y * 8 + x) * 4;
        r += d[i];
        g += d[i + 1];
        b += d[i + 2];
      }
    return deepen([r / 16 / 255, g / 16 / 255, b / 16 / 255]);
  };
  return [quad(0, 0), quad(4, 0), quad(0, 4), quad(4, 4)];
}

function deepen([r, g, b]: RGB): RGB {
  const m = (r + g + b) / 3;
  const sat = 1.35; // a little more colour, then darker
  const f = (v: number) => Math.max(0, Math.min(1, (m + (v - m) * sat) * 0.7));
  return [f(r), f(g), f(b)];
}
