# Desktop web interface

Choosen's plain JavaScript, HTML and CSS interface provides photo swiping, rankings, gallery browsing and settings. Windows opens it at / by default. Android bundles it as an optional interface alongside the Svelte interface.

- [index.html](index.html): screen structure.
- [app.js](app.js): interactions and application requests.
- [app.css](app.css): layout and styling.

The Windows executable embeds these files, and the Android app includes them as assets. Both interfaces use the same engine and rating data. See the [Svelte interface](../web3/) and [architecture guide](../docs/ARCHITECTURE.md).
