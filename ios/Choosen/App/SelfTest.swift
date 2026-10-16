// A self-test for the iOS Simulator in CI (.github/workflows/ios.yml), in Debug builds only. CI adds
// generated test photos to the simulator, then launches the app twice:
//   -ChoosenSelfTest first   makes two test albums, chooses the Photos library, rates one picture,
//                            and checks pictures, thumbnails and both ways of sending a POST body
//   -ChoosenSelfTest second  (a new process) checks that the rating is still there, and the language
// Each prints one line "CHOOSEN-SELFTEST <json>" and the app exits (0 = passed).
#if DEBUG
import Photos
import WebKit

enum SelfTest {
  static var phase: String? {
    let a = CommandLine.arguments
    guard let i = a.firstIndex(of: "-ChoosenSelfTest"), i + 1 < a.count else { return nil }
    return a[i + 1]
  }

  /// Starts the test in the page; it reports back through the page bridge ({op:"selftest"}). Only
  /// plain WebKit calls are used: the Swift-only async JavaScript call needs libswiftWebKit, which
  /// some simulator runtimes lack.
  static func run(_ web: WKWebView) {
    guard let phase else { return }
    prepareAlbums {
      DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
        let body = phase == "first" ? first : second
        let js = "(async () => { try {\n\(body)\n} catch (e) { return JSON.stringify({ passed: false, error: String(e) }); } })()"
          + ".then((r) => window.webkit.messageHandlers.choosen.postMessage({ op: 'selftest', result: r }));"
        web.evaluateJavaScript(js, completionHandler: nil)
      }
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + 150) { finish("{\"passed\":false,\"error\":\"timeout\"}") }
  }

  /// The page's result: printed for CI, then the app exits (0 = passed).
  static func finish(_ json: String) {
    print("CHOOSEN-SELFTEST \(json)")
    fflush(stdout)
    let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    try? Data(json.utf8).write(to: docs.appendingPathComponent("selftest-\(phase ?? "none").json"))
    Core.saveIndex()
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) { exit(json.contains("\"passed\":true") ? 0 : 1) }
  }

  /// Albums from the test photos: "Test A" (pictures 0–9), "Test B" (5–14, so five are in both) and
  /// "Test: A|B" (15–17), whose ':' and '|' mean something to the core and must not leak into it.
  private static func prepareAlbums(_ done: @escaping () -> Void) {
    PHPhotoLibrary.requestAuthorization(for: .readWrite) { _ in
      let have = PHAssetCollection.fetchAssetCollections(with: .album, subtype: .any, options: nil)
      var titles = Set<String>()
      have.enumerateObjects { c, _, _ in titles.insert(c.localizedTitle ?? "") }
      guard !titles.contains("Test A") else { return DispatchQueue.main.async(execute: done) }
      let assets = PHAsset.fetchAssets(with: .image, options: nil)
      var list: [PHAsset] = []
      assets.enumerateObjects { a, _, _ in list.append(a) }
      PHPhotoLibrary.shared().performChanges({
        let a = PHAssetCollectionChangeRequest.creationRequestForAssetCollection(withTitle: "Test A")
        a.addAssets(Array(list.prefix(10)) as NSArray)
        let b = PHAssetCollectionChangeRequest.creationRequestForAssetCollection(withTitle: "Test B")
        b.addAssets(Array(list.dropFirst(5).prefix(10)) as NSArray)
        let c = PHAssetCollectionChangeRequest.creationRequestForAssetCollection(withTitle: "Test: A|B")
        c.addAssets(Array(list.dropFirst(15).prefix(3)) as NSArray)
      }, completionHandler: { _, _ in DispatchQueue.main.async(execute: done) })
    }
  }

  private static let first = """
    const W = (ms) => new Promise((r) => setTimeout(r, ms));
    const J = async (u, o) => { const r = await fetch(u, o); return { status: r.status, body: await r.json() }; };
    const out = { phase: 'first' };
    out.pick = (await J('/api/pickfolder', { method: 'POST' })).body.path;
    out.config = (await J('/api/config?root=' + encodeURIComponent(out.pick), { method: 'POST' })).body;
    let st = null;
    for (let i = 0; i < 120; i++) {
      st = (await J('/api/state')).body;
      if (!st.scanning && st.items > 0) break;
      await W(500);
    }
    out.items = st.items;
    out.scanError = st.scanError;
    const folders = (await J('/api/folders')).body;
    out.folders = (JSON.stringify(folders).match(/"name":"[^"]*"/g) || []).map((s) => s.slice(8, -1));
    // the album "Test: A|B" is one folder, chosen alone by its name (':' and '|' became full-width)
    const odd = 'Test： A｜B';
    out.oddScope = (await J('/api/items?filter=all&limit=100&scope=' + encodeURIComponent(odd))).body.items.length;
    const card = (await J('/api/next?n=3')).body.items[0];
    out.card = { name: card.name, dir: card.dir };
    const img = await fetch('/img/' + card.id + '?v=' + card.fp + '&w=800');
    out.img = [img.status, img.headers.get('Content-Type'), (await img.blob()).size];
    const th = await fetch('/thumb/' + card.id + '?v=' + card.fp + '&s=200');
    out.thumb = [th.status, (await th.blob()).size];
    out.rate = (await J('/api/rate?id=' + card.id + '&fp=' + card.fp + '&a=L&cid=selftest-1', { method: 'POST' })).body;
    out.bridgePost = await window.webkit.messageHandlers.choosen.postMessage({ op: 'post', url: '/api/albums', body: '[{"bridge":1}]' });
    out.bridgeRead = (await J('/api/albums')).body;
    const viaFetch = await fetch('/api/albums', { method: 'POST', body: '[{"fetch":1}]' });
    out.fetchBody = [viaFetch.status, JSON.stringify((await J('/api/albums')).body)];
    out.refused = await window.webkit.messageHandlers.choosen.postMessage({ op: 'post', url: 'https://example.com/', body: '' }).then(() => 'answered', (e) => 'refused');
    out.version = (await J('/api/version')).body.v;
    out.icon = await window.webkit.messageHandlers.choosen.postMessage({ op: 'iconColor' });
    out.app = document.documentElement.classList.contains('app');
    out.rated = (await J('/api/state')).body.rated;
    out.passed = out.pick === '/photos' && out.config.ok === true && out.items >= 20 && !out.scanError &&
      ['Test A', 'Test B', odd, 'No album'].every((n) => out.folders.includes(n)) && out.oddScope === 3 &&
      out.img[0] === 200 && out.img[1] === 'image/jpeg' && out.img[2] > 1000 && out.thumb[0] === 200 && out.thumb[1] > 500 &&
      out.rated >= 1 && String(out.bridgePost).startsWith('200') && JSON.stringify(out.bridgeRead) === '[{"bridge":1}]' &&
      out.fetchBody[0] === 200 && out.fetchBody[1] === '[{"fetch":1}]' && out.refused === 'refused' && out.app &&
      out.icon === 'blue';
    return JSON.stringify(out);
    """

  private static let second = """
    const W = (ms) => new Promise((r) => setTimeout(r, ms));
    const out = { phase: 'second' };
    let st = null;
    for (let i = 0; i < 120; i++) {
      st = await (await fetch('/api/state')).json();
      if (!st.scanning) break;
      await W(500);
    }
    out.items = st.items;
    out.rated = st.rated;
    out.lang = document.documentElement.lang;
    await W(1500);
    out.nav = [...document.querySelectorAll('nav button')].map((b) => b.textContent.trim()).join('/');
    out.passed = out.rated >= 1 && out.items >= 20 && out.lang === 'pt-BR' && out.nav.startsWith('Galeria/');
    return JSON.stringify(out);
    """
}
#endif
