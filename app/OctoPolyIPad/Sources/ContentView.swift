import SwiftUI

struct ContentView: View {
    @StateObject private var model = MeshViewModel()

    var body: some View {
        VStack(spacing: 12) {
            MetalViewport(model: model)
                .background(Color.black)
                .clipShape(RoundedRectangle(cornerRadius: 14))
                .overlay {
                    RoundedRectangle(cornerRadius: 14)
                        .stroke(.secondary.opacity(0.4))
                }

            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    Button("Loop Cut") { model.loopCut() }
                    Button("Knife Cut") { model.knifeCut() }
                    Button("Inset") { model.inset() }
                    Button("Merge") { model.merge() }
                    Button("Extrude") { model.extrude() }
                    Button("Reset") { model.resetCube() }
                }
                .buttonStyle(.borderedProminent)
            }

            HStack {
                Text(model.status)
                    .lineLimit(2)
                Spacer()
                Text("Revision \(model.revision)")
                    .monospacedDigit()
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
        }
        .padding()
        .navigationTitle("OctoPoly")
    }
}

#Preview {
    ContentView()
}
