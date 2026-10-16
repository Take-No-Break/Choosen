// Choosen for iPhone: one full-screen web view with Choosen V3 over the shared C++ core.
import UIKit

@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
  var window: UIWindow?

  func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
    Core.start()
    let w = UIWindow(frame: UIScreen.main.bounds)
    w.rootViewController = WebViewController()
    w.makeKeyAndVisible()
    window = w
    return true
  }

  func applicationDidEnterBackground(_ application: UIApplication) {
    Core.saveIndex()  // the rating journal is written at once; the index only needs to be current
  }

  func applicationWillEnterForeground(_ application: UIApplication) {
    Core.rescan()  // pictures taken or albums changed meanwhile
  }
}
