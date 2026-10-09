import SwiftUI

struct PresetsView: View {
    @EnvironmentObject var state: PlayerState
    @Environment(\.dismiss) private var dismiss
    @State private var naming = false
    @State private var name = ""

    var body: some View {
        NavigationStack {
            List {
                Section("Built-in") {
                    ForEach(state.builtInPresets) { row($0) }
                }
                if !state.userPresets.isEmpty {
                    Section("My presets") {
                        ForEach(state.userPresets) { row($0) }
                            .onDelete { idx in idx.map { state.userPresets[$0] }.forEach(state.deletePreset) }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .navigationTitle("Presets")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Save Current") { naming = true }
                }
            }
            .alert("Save preset", isPresented: $naming) {
                TextField("Name", text: $name)
                Button("Save") {
                    state.saveCurrentAsPreset(named: name.isEmpty ? "My Preset" : name)
                    name = ""
                }
                Button("Cancel", role: .cancel) {}
            }
        }
    }

    private func row(_ p: Preset) -> some View {
        Button {
            state.apply(p)
            dismiss()
        } label: {
            HStack {
                Text(p.name).foregroundStyle(.primary)
                Spacer()
                if p.id == state.currentPresetID { Image(systemName: "checkmark").foregroundStyle(Theme.accent) }
            }
        }
        .listRowBackground(Color.white.opacity(0.04))
    }
}
