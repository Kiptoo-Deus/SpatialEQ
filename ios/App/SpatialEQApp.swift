import SwiftUI

@main
struct SpatialEQApp: App {
    @StateObject private var state = PlayerState()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(state)
                .environmentObject(state.headTracker)
                .onOpenURL { url in
                    // Audio files shared to SpatialEQ ("Open in…" / share sheet) are added to the library.
                    Task { await state.library.importFiles([url]) }
                }
        }
    }
}
