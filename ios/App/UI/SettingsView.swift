import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var state: PlayerState

    private var version: String {
        let info = Bundle.main.infoDictionary
        return "\(info?["CFBundleShortVersionString"] as? String ?? "?") (\(info?["CFBundleVersion"] as? String ?? "?"))"
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Playback") {
                    Toggle("Shuffle", isOn: $state.shuffle)
                    Toggle("Repeat all", isOn: $state.repeatAll)
                    Toggle("Effects on", isOn: $state.enabled)
                }

                Section {
                    LabeledContent("Output", value: state.routeName)
                } footer: {
                    Text("SpatialEQ remembers a separate sound for each headphone and speaker.")
                }

                Section("About") {
                    VStack(spacing: 6) {
                        Image(systemName: "dot.radiowaves.left.and.right")
                            .font(.system(size: 40))
                            .foregroundStyle(Theme.accent)
                        Text("SpatialEQ").font(.title2.bold())
                        Text("Version \(version)").font(.footnote).foregroundStyle(.secondary)
                        Text("Developed by Savannah DSP").font(.headline).padding(.top, 6)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                    Link(destination: URL(string: "https://savannahdsp.com")!) {
                        Label("savannahdsp.com", systemImage: "globe")
                    }
                    Link(destination: URL(string: "https://kiptoo-deus.github.io/SpatialEQ/privacy-policy.html")!) {
                        Label("Privacy policy", systemImage: "hand.raised")
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}
