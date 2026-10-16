// The shared C++ core (src/capi.h) for Swift. Its data (settings, index, rating journal, backups)
// lives in Application Support/Choosen, which stays on the phone (and in the phone's own backup).
import Foundation

enum Core {
  private static let queue = DispatchQueue(label: "choosen.core", qos: .userInitiated, attributes: .concurrent)

  static let dataDir: URL = {
    let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    let dir = base.appendingPathComponent("Choosen", isDirectory: true)
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    return dir
  }()

  /// Loads the library and starts reading the photo folders (once).
  static func start() {
    let held = take(choosen_init(dataDir.path))
    if !held.isEmpty { NSLog("Choosen: data folder in use by %@", held) }
    queue.async { choosen_daily_backup() }
  }

  /// One page API call: (HTTP status, JSON body). Runs off the main thread.
  static func api(method: String, path: String, query: String, body: String, done: @escaping (Int, String) -> Void) {
    queue.async {
      let res = take(choosen_api(method, path, query, body))
      let (status, json) = split(res)
      done(status, json)
    }
  }

  /// "<status>\n<json>" as the Android bridge answers it (for the page's POST bridge).
  static func apiRaw(method: String, path: String, query: String, body: String, done: @escaping (String) -> Void) {
    queue.async { done(take(choosen_api(method, path, query, body))) }
  }

  /// The picture path of an item ("" if unknown or gone).
  static func itemPath(id: Int32, fp: String) -> String { take(choosen_item_path(id, fp)) }

  static var version: UInt64 { choosen_version() }

  static func saveIndex() { queue.async { choosen_save_index() } }

  static func rescan() { queue.async { choosen_rescan() } }

  private static func take(_ p: UnsafeMutablePointer<CChar>?) -> String {
    guard let p else { return "" }
    defer { choosen_free(p) }
    return String(cString: p)
  }

  private static func split(_ res: String) -> (Int, String) {
    guard let nl = res.firstIndex(of: "\n") else { return (500, "{\"error\":\"internal\"}") }
    return (Int(res[..<nl]) ?? 500, String(res[res.index(after: nl)...]))
  }
}
