// Answers every request of the page at choosen://app/ inside the app; nothing goes to a network.
//   /v3/…            Choosen V3 (web3/dist, bundled as "v3")
//   /api/…           the core's page API (src/api_routes.cpp)
//   /api/version     the core's change counter, which the page polls instead of /api/events
//   /api/pickfolder  asks for access to the Photos library; the photo folder is then "/photos"
//   /img/<id>, /thumb/<id>  a picture from the Photos library as JPEG, fitted to the asked size
import Photos
import UIKit
import WebKit

final class SchemeHandler: NSObject, WKURLSchemeHandler {
  static let scheme = "choosen"
  static let home = URL(string: "choosen://app/v3/")!
  static let photosAllowed = Notification.Name("ChoosenPhotosAllowed")

  private let lock = NSLock()
  private var live = Set<ObjectIdentifier>()  // tasks WebKit has not stopped
  private let thumbs = NSCache<NSString, NSData>()
  private let imageQueue = DispatchQueue(label: "choosen.images", qos: .userInitiated, attributes: .concurrent)
  private let fitDecodes = DispatchSemaphore(value: 2)  // large pictures: at most two at once (memory)

  override init() {
    thumbs.totalCostLimit = 48 << 20
  }

  func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
    lock.lock()
    live.insert(ObjectIdentifier(task))
    lock.unlock()
    guard let url = task.request.url else { return finish(task, 400, "{}") }
    let path = url.path
    let query = url.query ?? ""
    let params = Dictionary(URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?.map { ($0.name, $0.value ?? "") } ?? [],
                            uniquingKeysWith: { a, _ in a })

    switch path {
    case "/", "/v3", "/v3/", "/v3/index.html":
      return page(task)
    case "/v3/app.js", "/v3/app.css":
      return bundled(task, String(path.dropFirst(4)))
    case "/api/version":
      return finish(task, 200, "{\"v\":\(Core.version)}")
    case "/api/pickfolder":
      return pickFolder(task)
    case "/api/events", "/api/dirs":
      return finish(task, 404, "{}")  // not used in the app (the page polls /api/version)
    default:
      break
    }
    if path.hasPrefix("/img/") || path.hasPrefix("/thumb/") {
      let thumb = path.hasPrefix("/thumb/")
      guard let id = Int32(path.split(separator: "/").last ?? "") else { return finish(task, 404, "{}") }
      return image(task, id: id, fp: params["v"] ?? "", thumb: thumb,
                   size: Int(thumb ? (params["s"] ?? "320") : (params["w"] ?? "2560")) ?? 2560)
    }
    if path.hasPrefix("/api/") {
      let body = task.request.httpBody.flatMap { String(data: $0, encoding: .utf8) } ?? ""
      Core.api(method: task.request.httpMethod ?? "GET", path: path, query: query, body: body) { status, json in
        self.finish(task, status, json)
      }
      return
    }
    finish(task, 404, "{}")
  }

  func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
    lock.lock()
    live.remove(ObjectIdentifier(task))
    lock.unlock()
  }

  // MARK: - answers

  /// Choosen V3's page marked as the app's own (class="app"), like the Android app: its launch
  /// screen shows at once.
  private func page(_ task: WKURLSchemeTask) {
    guard let url = Bundle.main.url(forResource: "index", withExtension: "html", subdirectory: "v3"),
          var html = try? String(contentsOf: url, encoding: .utf8)
    else { return finish(task, 500, "{\"error\":\"page missing\"}") }
    html = html.replacingOccurrences(of: "<html lang=\"ja\">", with: "<html lang=\"ja\" class=\"app\">")
    send(task, 200, "text/html; charset=utf-8", Data(html.utf8))
  }

  private func bundled(_ task: WKURLSchemeTask, _ name: String) {
    let ext = (name as NSString).pathExtension
    guard let url = Bundle.main.url(forResource: (name as NSString).deletingPathExtension, withExtension: ext, subdirectory: "v3"),
          let data = try? Data(contentsOf: url)
    else { return finish(task, 404, "{}") }
    send(task, 200, ext == "js" ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8", data)
  }

  /// Asks for the Photos library (once; later the answer is known). Denied: opens the app's page in
  /// Settings, where access can be turned on, and answers "" (no folder chosen).
  private func pickFolder(_ task: WKURLSchemeTask) {
    PHPhotoLibrary.requestAuthorization(for: .readWrite) { status in
      if status == .authorized || status == .limited {
        Core.rescan()
        NotificationCenter.default.post(name: Self.photosAllowed, object: nil)
        self.finish(task, 200, "{\"path\":\"\(ChoosenPhotos.root())\"}")
      } else {
        DispatchQueue.main.async {
          if let s = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(s) }
        }
        self.finish(task, 200, "{\"path\":\"\"}")
      }
    }
  }

  private func image(_ task: WKURLSchemeTask, id: Int32, fp: String, thumb: Bool, size: Int) {
    let px = thumb ? max(64, min(1024, size)) : max(480, min(2560, size))
    imageQueue.async {
      let file = Core.itemPath(id: id, fp: fp)
      guard !file.isEmpty, let assetId = ChoosenPhotos.assetId(forPath: file),
            let asset = PHAsset.fetchAssets(withLocalIdentifiers: [assetId], options: nil).firstObject
      else { return self.finish(task, 404, "{}") }
      // an edit in Photos changes the modification date, so it gets a new thumbnail
      let edited = asset.modificationDate?.timeIntervalSince1970 ?? 0
      let key = "\(assetId)|\(edited)|\(px)" as NSString
      if thumb, let hit = self.thumbs.object(forKey: key) { return self.send(task, 200, "image/jpeg", hit as Data) }
      if !thumb { self.fitDecodes.wait() }
      let o = PHImageRequestOptions()
      o.isSynchronous = true  // on imageQueue, never on the main thread
      o.deliveryMode = .highQualityFormat
      o.resizeMode = thumb ? .fast : .exact
      o.isNetworkAccessAllowed = true  // a picture kept only in iCloud is fetched like the Photos app does
      o.version = .current  // with the user's edits
      var jpeg: Data?
      PHImageManager.default().requestImage(for: asset, targetSize: CGSize(width: px, height: px), contentMode: .aspectFit, options: o) { img, _ in
        jpeg = img?.jpegData(compressionQuality: thumb ? 0.8 : 0.88)
      }
      if !thumb { self.fitDecodes.signal() }
      guard let jpeg else { return self.finish(task, 404, "{}") }
      if thumb { self.thumbs.setObject(jpeg as NSData, forKey: key, cost: jpeg.count) }
      self.send(task, 200, "image/jpeg", jpeg)
    }
  }

  // MARK: - sending

  private func finish(_ task: WKURLSchemeTask, _ status: Int, _ json: String) {
    send(task, status, "application/json; charset=utf-8", Data(json.utf8))
  }

  /// Sends a whole answer on the main thread, unless WebKit has stopped the task meanwhile.
  private func send(_ task: WKURLSchemeTask, _ status: Int, _ type: String, _ data: Data) {
    DispatchQueue.main.async {
      self.lock.lock()
      let alive = self.live.remove(ObjectIdentifier(task)) != nil
      self.lock.unlock()
      guard alive, let url = task.request.url,
            let res = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1",
                                      headerFields: ["Content-Type": type, "Cache-Control": "no-store", "Content-Length": String(data.count)])
      else { return }
      task.didReceive(res)
      task.didReceive(data)
      task.didFinish()
    }
  }
}
