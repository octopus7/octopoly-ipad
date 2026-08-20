import Combine
import Foundation

struct SceneOutlinerRow: Identifiable, Equatable {
    let id: UInt64
    let name: String
    let isSelected: Bool
}

@MainActor
final class MeshViewModel: ObservableObject {
    @Published private(set) var vertexData = Data()
    @Published private(set) var status = "Scene ready: one selected Cube"
    @Published private(set) var outlinerItems: [SceneOutlinerRow] = []
    @Published private(set) var selectedObjectId: UInt64 = 0
    @Published private(set) var sceneRevision: UInt64 = 0

    private let bridge: MeshBridge?

    private var documentsURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }
    private var projectURL: URL {
        documentsURL.appendingPathComponent("OctoPoly.octoscene", isDirectory: false)
    }
    private var legacyProjectURL: URL {
        documentsURL.appendingPathComponent("OctoPoly.octopoly", isDirectory: false)
    }
    private var glbURL: URL {
        documentsURL.appendingPathComponent("OctoPoly.glb", isDirectory: false)
    }

    init() {
        bridge = MeshBridge()
        guard bridge != nil else {
            status = "Scene initialization failed: native Scene or snapshot allocation unavailable"
            return
        }
        if !refreshSceneState() {
            status = "Initial scene refresh failed"
        }
    }

    func saveScene() {
        guard let bridge else {
            status = "OCTOSCNE save failed: scene bridge is unavailable"
            return
        }
        guard let encoded = bridge.encodedProjectData else {
            status = "OCTOSCNE save failed: \(bridge.lastError)"
            return
        }
        do {
            try (encoded as Data).write(to: projectURL, options: .atomic)
            status = "Saved complete OCTOSCNE scene to OctoPoly.octoscene"
        } catch {
            status = "OCTOSCNE save failed: \(error.localizedDescription)"
        }
    }

    func loadScene() {
        guard let bridge else {
            status = "Scene load failed: scene bridge is unavailable"
            return
        }
        let sourceURL: URL
        if FileManager.default.fileExists(atPath: projectURL.path) {
            sourceURL = projectURL
        } else if FileManager.default.fileExists(atPath: legacyProjectURL.path) {
            sourceURL = legacyProjectURL
        } else {
            status = "Scene load failed: no OctoPoly.octoscene or legacy OctoPoly.octopoly file"
            return
        }

        do {
            let data = try Data(contentsOf: sourceURL)
            guard bridge.loadProjectData(data) else {
                status = "Scene load failed: \(bridge.lastError)"
                return
            }
            guard refreshSceneState() else {
                status = "Scene loaded but refresh failed: \(bridge.lastError)"
                return
            }
            status = bridge.loadedLegacyProject
                ? "Loaded legacy OCTOPOLY mesh as one selected scene object; save to migrate to OCTOSCNE"
                : "Loaded complete OCTOSCNE scene from OctoPoly.octoscene"
        } catch {
            status = "Scene load failed: \(error.localizedDescription)"
        }
    }

    func exportSelectedGlb() {
        guard let bridge else {
            status = "Selected-object GLB export failed: scene bridge is unavailable"
            return
        }
        guard let encoded = bridge.encodedGlbData else {
            status = "Selected-object GLB export failed: \(bridge.lastError)"
            return
        }
        do {
            try (encoded as Data).write(to: glbURL, options: .atomic)
            status = "Exported selected object only to OctoPoly.glb (polygons triangulated)"
        } catch {
            status = "Selected-object GLB export failed: \(error.localizedDescription)"
        }
    }

    func importGlbAsNewObject() {
        guard let bridge else {
            status = "GLB import failed: scene bridge is unavailable"
            return
        }
        do {
            let data = try Data(contentsOf: glbURL)
            guard bridge.loadGlbData(data) else {
                status = "GLB import failed: \(bridge.lastError)"
                return
            }
            guard refreshSceneState() else {
                status = "GLB imported but scene refresh failed: \(bridge.lastError)"
                return
            }
            let policy = "Imported GLB as new selected ‘Imported GLB’ object; geometry only"
            status = bridge.glbDiagnostics.isEmpty
                ? policy
                : "\(policy). Loss diagnostics: \(bridge.glbDiagnostics)"
        } catch {
            status = "GLB import failed: \(error.localizedDescription)"
        }
    }

    func resetScene() {
        performMutation("Reset to one selected Cube") { $0.resetSceneCube() }
    }

    func selectObject(_ objectId: UInt64) {
        performMutation("Selected object \(objectId)") {
            $0.selectObject(objectId)
        }
    }

    func deleteSelectedObject() {
        guard selectedObjectId != 0 else {
            status = "Delete failed: no scene object is selected"
            return
        }
        let deleting = selectedObjectId
        performMutation("Deleted object \(deleting)") {
            $0.deleteObject(deleting)
        }
    }

    func renameSelectedObject(to name: String) {
        guard selectedObjectId != 0 else {
            status = "Rename failed: no scene object is selected"
            return
        }
        let selected = selectedObjectId
        performMutation("Renamed object \(selected) to \(name)") {
            $0.renameObject(selected, name: name)
        }
    }

    func addCube() { addPrimitive(.cube, name: "Cube") }
    func addPlane() { addPrimitive(.plane, name: "Plane") }
    func addTetrahedron() { addPrimitive(.tetrahedron, name: "Tetrahedron") }
    func addCylinder() { addPrimitive(.cylinder, name: "Cylinder") }
    func addCone() { addPrimitive(.cone, name: "Cone") }
    func addUVSphere() { addPrimitive(.uvSphere, name: "UV Sphere") }

    func translateX(_ delta: Double) {
        performMutation("Translated selected object X by \(delta)") {
            $0.translateSelected(byX: delta, y: 0, z: 0)
        }
    }

    func translateY(_ delta: Double) {
        performMutation("Translated selected object Y by \(delta)") {
            $0.translateSelected(byX: 0, y: delta, z: 0)
        }
    }

    func translateZ(_ delta: Double) {
        performMutation("Translated selected object Z by \(delta)") {
            $0.translateSelected(byX: 0, y: 0, z: delta)
        }
    }

    func rotateX() { rotate(axisX: 1, y: 0, z: 0, label: "X") }
    func rotateY() { rotate(axisX: 0, y: 1, z: 0, label: "Y") }
    func rotateZ() { rotate(axisX: 0, y: 0, z: 1, label: "Z") }

    func scaleUp() {
        performMutation("Scaled selected object up by 10%") {
            $0.scaleSelected(byX: 1.1, y: 1.1, z: 1.1)
        }
    }

    func scaleDown() {
        performMutation("Scaled selected object down by 10%") {
            $0.scaleSelected(byX: 0.9, y: 0.9, z: 0.9)
        }
    }

    func loopCut() { performMutation("Loop Cut on selected object") { $0.loopCut() } }
    func knifeCut() { performMutation("Knife Cut on selected object") { $0.knifeCut() } }
    func inset() { performMutation("Inset on selected object") { $0.inset() } }
    func merge() { performMutation("Merge on selected object") { $0.merge() } }
    func extrude() { performMutation("Extrude on selected object") { $0.extrude() } }

    private func addPrimitive(_ primitive: MeshBridgePrimitive, name: String) {
        performMutation("Added and selected \(name)") {
            $0.addPrimitive(primitive)
        }
    }

    private func rotate(axisX: Double, y: Double, z: Double, label: String) {
        let radians = Double.pi / 12
        performMutation("Rotated selected object +15° around \(label)") {
            $0.rotateSelected(aroundAxisX: axisX, y: y, z: z, radians: radians)
        }
    }

    private func performMutation(_ successStatus: String,
                                 operation: (MeshBridge) -> Bool) {
        guard let bridge else {
            status = "Operation failed: scene bridge is unavailable"
            return
        }
        guard operation(bridge) else {
            status = "Operation failed: \(bridge.lastError)"
            return
        }
        guard refreshSceneState() else {
            status = "Operation succeeded but scene refresh failed: \(bridge.lastError)"
            return
        }
        status = successStatus
    }

    @discardableResult
    private func refreshSceneState() -> Bool {
        guard let bridge else { return false }
        let geometry = bridge.triangleVertexData as Data
        let rows = bridge.outlinerItems.map {
            SceneOutlinerRow(id: UInt64($0.objectId), name: $0.name, isSelected: $0.isSelected)
        }
        let selected = UInt64(bridge.selectedObjectId)
        let revision = UInt64(bridge.sceneRevision)
        vertexData = geometry
        outlinerItems = rows
        selectedObjectId = selected
        sceneRevision = revision
        return true
    }
}
