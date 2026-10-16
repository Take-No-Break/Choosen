# Interactive photo interface

Choosen's Svelte and TypeScript interface provides swiping, rankings, the gallery, slideshows and settings. Android opens it by default. Windows serves it at /v3/ and opens it with Choosen.exe --ui v3.

## Entry points

- [src/main.ts](src/main.ts) and [src/App.svelte](src/App.svelte): application startup and composition.
- [src/Swipe.svelte](src/Swipe.svelte): photo rating screen.
- [src/Gallery.svelte](src/Gallery.svelte): gallery browsing.
- [src/Story.svelte](src/Story.svelte): playback and slideshows.
- [src/Settings.svelte](src/Settings.svelte): preferences.
- [src/lib/api.ts](src/lib/api.ts): communication with the shared application API.
- [vite.config.ts](vite.config.ts): embedded interface build configuration.

Run npm ci, then npm run build to produce the bundled interface in dist/. See [platform build instructions](../docs/ARCHITECTURE.md).
