// Choosen V3 is served by Choosen.exe at /v3/ from three files embedded in the exe, so the build
// writes exactly index.html, app.js and app.css (no hashes, no chunks). The dev server proxies the
// core's routes to the dev instance with the test photos (port 47814).
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { readFileSync } from 'node:fs';

// the version shown in 設定 → About: the exe's own (res/choosen.rc), so there is one place to bump
const version = /"ProductVersion",\s*"([^"]+)"/.exec(readFileSync(new URL('../res/choosen.rc', import.meta.url), 'utf8'))?.[1] ?? '?';

const core = 'http://127.0.0.1:47814';

export default defineConfig({
  base: '/v3/',
  plugins: [svelte()],
  define: { __APP_VERSION__: JSON.stringify(version) },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    assetsDir: '',
    cssCodeSplit: false,
    modulePreload: false,
    assetsInlineLimit: 100_000, // everything inside app.js / app.css
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        entryFileNames: 'app.js',
        assetFileNames: (a) => (a.names?.[0]?.endsWith('.css') ? 'app.css' : '[name][extname]'),
      },
    },
  },
  server: {
    host: '127.0.0.1',
    port: 5173,
    proxy: Object.fromEntries(
      ['/api', '/img', '/thumb'].map((p) => [
        p,
        {
          target: core,
          changeOrigin: true, // Host = 127.0.0.1:47814, as the core's server requires
          configure: (proxy: any) =>
            proxy.on('proxyReq', (req: any) => {
              req.removeHeader('origin'); // the core accepts only its own origin
              req.removeHeader('referer');
            }),
        },
      ]),
    ),
  },
});
