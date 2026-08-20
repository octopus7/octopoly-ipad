import SwiftUI

struct ContentView: View {
    @StateObject private var model = MeshViewModel()

    var body: some View {
        GeometryReader { geometry in
            if geometry.size.width >= 760 {
                HStack(spacing: 16) {
                    SceneOutliner(model: model)
                        .frame(width: 280)
                    EditorWorkspace(model: model)
                }
                .padding()
            } else {
                VStack(spacing: 12) {
                    EditorWorkspace(model: model)
                    SceneOutliner(model: model)
                        .frame(height: 250)
                }
                .padding()
            }
        }
        .navigationTitle("OctoPoly Scene Editor")
    }
}

private struct SceneOutliner: View {
    @ObservedObject var model: MeshViewModel
    @State private var renameText = ""

    private struct RenameSyncKey: Equatable {
        let id: UInt64
        let name: String
    }

    private var selectedName: String {
        model.outlinerItems.first(where: { $0.id == model.selectedObjectId })?.name ?? ""
    }

    private var renameSyncKey: RenameSyncKey {
        RenameSyncKey(id: model.selectedObjectId, name: selectedName)
    }

    var body: some View {
        GroupBox("Scene Outliner") {
            VStack(alignment: .leading, spacing: 10) {
                List(model.outlinerItems) { item in
                    Button {
                        model.selectObject(item.id)
                    } label: {
                        HStack(spacing: 8) {
                            Image(systemName: item.isSelected ? "checkmark.circle.fill" : "circle")
                                .foregroundStyle(item.isSelected ? Color.accentColor : Color.secondary)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(item.name)
                                    .foregroundStyle(.primary)
                                Text("Object ID \(item.id)")
                                    .font(.caption2.monospacedDigit())
                                    .foregroundStyle(.secondary)
                            }
                            Spacer()
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Select \(item.name), object ID \(item.id)")
                }
                .listStyle(.plain)

                Divider()

                TextField("Selected object name", text: $renameText)
                    .textFieldStyle(.roundedBorder)
                    .disabled(model.selectedObjectId == 0)
                    .onSubmit { model.renameSelectedObject(to: renameText) }

                HStack {
                    Button("Rename") { model.renameSelectedObject(to: renameText) }
                        .disabled(model.selectedObjectId == 0 || renameText.isEmpty)
                    Button("Delete", role: .destructive) { model.deleteSelectedObject() }
                        .disabled(model.selectedObjectId == 0)
                }
                .buttonStyle(.bordered)
            }
            .onAppear { renameText = renameSyncKey.name }
            .onChange(of: renameSyncKey) { _, newKey in renameText = newKey.name }
        }
    }
}

private struct EditorWorkspace: View {
    @ObservedObject var model: MeshViewModel

    var body: some View {
        VStack(spacing: 12) {
            MetalViewport(model: model)
                .background(Color.black)
                .clipShape(RoundedRectangle(cornerRadius: 14))
                .overlay {
                    RoundedRectangle(cornerRadius: 14)
                        .stroke(.secondary.opacity(0.4))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .layoutPriority(1)

            ScrollView(.vertical, showsIndicators: true) {
                VStack(spacing: 10) {
                    fileControls
                    primitiveControls
                    transformControls
                    meshEditControls
                }
            }
            .frame(maxHeight: 290)

            HStack(alignment: .top) {
                Text(model.status)
                    .lineLimit(3)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text("Scene rev \(model.sceneRevision)")
                    .monospacedDigit()
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
        }
    }

    private var fileControls: some View {
        GroupBox("Scene & Interchange") {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    Button("Save OCTOSCNE Scene") { model.saveScene() }
                    Button("Load OCTOSCNE / Legacy") { model.loadScene() }
                    Button("Export Selected GLB") { model.exportSelectedGlb() }
                    Button("Import GLB as New Object") { model.importGlbAsNewObject() }
                    Button("Reset Scene") { model.resetScene() }
                }
                .buttonStyle(.borderedProminent)
            }
        }
    }

    private var primitiveControls: some View {
        GroupBox("Add Primitive") {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 105), spacing: 8)], spacing: 8) {
                Button("Cube") { model.addCube() }
                Button("Plane") { model.addPlane() }
                Button("Tetrahedron") { model.addTetrahedron() }
                Button("Cylinder") { model.addCylinder() }
                Button("Cone") { model.addCone() }
                Button("UV Sphere") { model.addUVSphere() }
            }
            .buttonStyle(.bordered)
        }
    }

    private var transformControls: some View {
        GroupBox("Selected Object Transform") {
            VStack(alignment: .leading, spacing: 8) {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack {
                        Text("Move 0.25")
                        Button("−X") { model.translateX(-0.25) }
                        Button("+X") { model.translateX(0.25) }
                        Button("−Y") { model.translateY(-0.25) }
                        Button("+Y") { model.translateY(0.25) }
                        Button("−Z") { model.translateZ(-0.25) }
                        Button("+Z") { model.translateZ(0.25) }
                    }
                }
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack {
                        Text("Rotate +15°")
                        Button("X") { model.rotateX() }
                        Button("Y") { model.rotateY() }
                        Button("Z") { model.rotateZ() }
                        Divider().frame(height: 24)
                        Text("Uniform Scale")
                        Button("−10%") { model.scaleDown() }
                        Button("+10%") { model.scaleUp() }
                    }
                }
            }
            .buttonStyle(.bordered)
        }
    }

    private var meshEditControls: some View {
        GroupBox("Selected Mesh Editing") {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    Button("Loop Cut") { model.loopCut() }
                    Button("Knife Cut") { model.knifeCut() }
                    Button("Inset") { model.inset() }
                    Button("Merge") { model.merge() }
                    Button("Extrude") { model.extrude() }
                }
                .buttonStyle(.bordered)
            }
        }
    }
}

#Preview {
    ContentView()
}
