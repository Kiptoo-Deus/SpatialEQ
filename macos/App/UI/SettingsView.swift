import SwiftUI

/// The app's Settings window (⌘,): general options and About.
struct SettingsView: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        TabView {
            Form {
                Section("Startup") {
                    Toggle("Launch SpatialEQ at login", isOn: $state.launchAtLogin)
                }
                Section("Output") {
                    DevicePicker()
                    Text("“System Default Output” follows the device chosen in macOS Sound settings.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Section("Privacy") {
                    Button("Open System Audio Recording settings…") {
                        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture")!)
                    }
                    Text("Audio is processed on this Mac and never recorded or sent anywhere.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
            .tabItem { Label("General", systemImage: "gearshape") }

            AboutView()
                .tabItem { Label("About", systemImage: "info.circle") }
        }
        .frame(width: 460, height: 380)
    }
}

struct AboutView: View {
    private var version: String {
        let info = Bundle.main.infoDictionary
        let v = info?["CFBundleShortVersionString"] as? String ?? "?"
        let b = info?["CFBundleVersion"] as? String ?? "?"
        return "Version \(v) (\(b))"
    }

    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: "dot.radiowaves.left.and.right")
                .font(.system(size: 44))
                .foregroundStyle(Theme.accent)
                .padding(.top, 16)
            Text("SpatialEQ").font(.title.bold())
            Text(version).font(.callout).foregroundStyle(.secondary)
            Divider().padding(.horizontal, 60).padding(.vertical, 6)
            Text("Developed by Savannah DSP").font(.headline)
            Link("savannahdsp.com", destination: URL(string: "https://savannahdsp.com")!)
            Link("Privacy policy", destination: URL(string: "https://kiptoo-deus.github.io/SpatialEQ/privacy-policy.html")!)
                .font(.callout)
            Spacer()
            Text("© 2026 Savannah DSP").font(.caption).foregroundStyle(.secondary).padding(.bottom, 12)
        }
        .frame(maxWidth: .infinity)
    }
}
