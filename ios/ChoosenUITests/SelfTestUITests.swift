// Runs the app's self-test (ios/Choosen/App/SelfTest.swift) in the iOS Simulator and answers the
// system's photo-access prompt the way a person would (simctl cannot grant full Photos access on
// recent runtimes). The verdict is in the files the app writes (selftest-<phase>.json), which the CI
// workflow reads; this test only drives the launches.
import XCTest

final class SelfTestUITests: XCTestCase {
  func testSelfTest() {
    run(["-ChoosenSelfTest", "first"])
    // a new process: the rating must still be there; and the screens in Portuguese
    run(["-ChoosenSelfTest", "second", "-AppleLanguages", "(pt-BR)", "-AppleLocale", "pt_BR"])
  }

  private func run(_ arguments: [String]) {
    let app = XCUIApplication()
    app.launchArguments = arguments
    app.launch()
    let springboard = XCUIApplication(bundleIdentifier: "com.apple.springboard")
    let allow = ["Allow Full Access", "Allow Access to All Photos", "Permitir Acesso Total", "フルアクセスを許可"]
    let deadline = Date().addingTimeInterval(170)
    while Date() < deadline, app.state != .notRunning {
      for owner in [springboard, app] {
        for label in allow where owner.alerts.buttons[label].exists {
          owner.alerts.buttons[label].tap()
        }
      }
      sleep(1)
    }
    XCTAssertEqual(app.state, .notRunning, "the self-test did not finish: \(arguments)")
  }
}
