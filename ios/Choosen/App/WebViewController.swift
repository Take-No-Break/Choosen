// The full-screen web view and the page's bridge to the app (window.webkit.messageHandlers.choosen).
// The bridge answers only the app's own page (choosen://app, main frame) and only small inputs:
//   {op:"post", url, body}       a POST with a body to the core ("<status>\n<json>")
//   {op:"saveImage", data, name} a collage (base64 JPEG) added to the Photos library ("ok" or "")
//   {op:"openSettings"}          this app's page in the Settings app (photo access)
//   {op:"iconColor"}             the home screen icon: "blue" or "purple"
//   {op:"setIconColor", color}   switches it (iOS shows a short notice)
import Photos
import UIKit
import WebKit

final class WebViewController: UIViewController, WKScriptMessageHandlerWithReply, PHPhotoLibraryChangeObserver {
  private let handler = SchemeHandler()
  private var web: WKWebView!
  private var rescanPending = false
  private var observing = false

  static let maxPostBody = 2 << 20     // saved conditions and settings are a few KB
  static let maxImageBase64 = 48 << 20  // a collage is a few MB

  override func loadView() {
    let config = WKWebViewConfiguration()
    config.setURLSchemeHandler(handler, forURLScheme: SchemeHandler.scheme)
    config.userContentController.addScriptMessageHandler(self, contentWorld: .page, name: "choosen")
    config.allowsInlineMediaPlayback = true
    web = WKWebView(frame: .zero, configuration: config)
    web.isOpaque = false
    web.backgroundColor = .white  // the launch screen's colour, until the page draws
    web.scrollView.contentInsetAdjustmentBehavior = .never
    web.scrollView.bounces = false
    web.allowsBackForwardNavigationGestures = false
    #if DEBUG
    if #available(iOS 16.4, *) { web.isInspectable = true }
    #endif
    view = web
  }

  override func viewDidLoad() {
    super.viewDidLoad()
    observeIfAllowed()
    NotificationCenter.default.addObserver(forName: SchemeHandler.photosAllowed, object: nil, queue: .main) { [weak self] _ in
      self?.observeIfAllowed()
    }
    web.load(URLRequest(url: SchemeHandler.home))
    #if DEBUG
    SelfTest.run(web)
    #endif
  }

  /// Watches the library for changes, once access is given (registering earlier could ask for it).
  private func observeIfAllowed() {
    let s = PHPhotoLibrary.authorizationStatus(for: .readWrite)
    guard !observing, s == .authorized || s == .limited else { return }
    observing = true
    PHPhotoLibrary.shared().register(self)
  }

  override var prefersStatusBarHidden: Bool { true }
  override var prefersHomeIndicatorAutoHidden: Bool { true }

  /// Pictures added, removed or moved between albums: read the library again (at most every 2 s).
  func photoLibraryDidChange(_ changeInstance: PHChange) {
    DispatchQueue.main.async {
      guard !self.rescanPending else { return }
      self.rescanPending = true
      DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
        self.rescanPending = false
        Core.rescan()
      }
    }
  }

  func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage,
                             replyHandler: @escaping (Any?, String?) -> Void) {
    let origin = message.frameInfo.securityOrigin
    guard message.frameInfo.isMainFrame, origin.protocol == SchemeHandler.scheme, origin.host == "app",
          let m = message.body as? [String: Any], let op = m["op"] as? String
    else { return replyHandler(nil, "refused") }
    switch op {
    case "post":
      guard let url = m["url"] as? String, let body = m["body"] as? String, body.utf8.count <= Self.maxPostBody,
            let u = URLComponents(string: url), u.path.hasPrefix("/api/")
      else { return replyHandler(nil, "bad request") }
      Core.apiRaw(method: "POST", path: u.path, query: u.percentEncodedQuery ?? "", body: body) { res in
        DispatchQueue.main.async { replyHandler(res, nil) }
      }
    case "saveImage":
      guard let b64 = m["data"] as? String, b64.utf8.count <= Self.maxImageBase64, let data = Data(base64Encoded: b64),
            UIImage(data: data) != nil
      else { return replyHandler("", nil) }
      saveToPhotos(data) { ok in DispatchQueue.main.async { replyHandler(ok ? "ok" : "", nil) } }
    #if DEBUG
    case "selftest":
      SelfTest.finish(m["result"] as? String ?? "{\"passed\":false}")
      replyHandler("ok", nil)
    #endif
    case "iconColor":
      replyHandler(UIApplication.shared.alternateIconName == "AppIconPurple" ? "purple" : "blue", nil)
    case "setIconColor":
      guard let color = m["color"] as? String, color == "blue" || color == "purple", UIApplication.shared.supportsAlternateIcons
      else { return replyHandler("", nil) }
      UIApplication.shared.setAlternateIconName(color == "purple" ? "AppIconPurple" : nil) { error in
        replyHandler(error == nil ? "ok" : "", nil)
      }
    case "openSettings":
      if let s = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(s) }
      replyHandler("ok", nil)
    default:
      replyHandler(nil, "unknown")
    }
  }

  /// Adds a picture to the Photos library (asks for add-only access once). Nothing else is touched.
  private func saveToPhotos(_ jpeg: Data, done: @escaping (Bool) -> Void) {
    PHPhotoLibrary.requestAuthorization(for: .addOnly) { status in
      guard status == .authorized || status == .limited else { return done(false) }
      PHPhotoLibrary.shared().performChanges({
        PHAssetCreationRequest.forAsset().addResource(with: .photo, data: jpeg, options: nil)
      }, completionHandler: { ok, _ in done(ok) })
    }
  }
}
