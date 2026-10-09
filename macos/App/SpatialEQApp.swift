import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    let state = AppState()
    private var capture: CaptureMode?

    func applicationDidFinishLaunching(_ notification: Notification) {
        if let dir = CaptureMode.outputDirectory {
            capture = CaptureMode(dir: dir, state: state)
            capture?.run()
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Destroying the tap un-mutes system audio immediately.
        state.shutdown()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false // keep running from the menu bar
    }
}

@main
struct SpatialEQApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        Window("SpatialEQ", id: "main") {
            MainView()
                .environmentObject(delegate.state)
                .environmentObject(delegate.state.meters)
                .environmentObject(delegate.state.headTracker)
                .frame(minWidth: 1080, minHeight: 720)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 820)

        MenuBarExtra {
            MenuBarPanel()
                .environmentObject(delegate.state)
                .environmentObject(delegate.state.meters)
        } label: {
            Image(systemName: "dot.radiowaves.left.and.right")
        }
        .menuBarExtraStyle(.window)
    }
}
