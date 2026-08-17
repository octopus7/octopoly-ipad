import Combine
import Foundation

@MainActor
final class MeshViewModel: ObservableObject {
    @Published private(set) var vertexData = Data()
    @Published private(set) var status = "Cube ready"
    @Published private(set) var revision: UInt = 0

    private let bridge = MeshBridge()

    init() {
        refreshGeometry()
    }

    func resetCube() {
        bridge.resetCube()
        refreshGeometry()
        status = "Cube reset"
    }

    func loopCut() {
        perform("Loop Cut") { bridge.loopCut() }
    }

    func knifeCut() {
        perform("Knife Cut") { bridge.knifeCut() }
    }

    func inset() {
        perform("Inset") { bridge.inset() }
    }

    func merge() {
        perform("Merge") { bridge.merge() }
    }

    func extrude() {
        perform("Extrude") { bridge.extrude() }
    }

    private func perform(_ name: String, operation: () -> Bool) {
        let succeeded = operation()
        refreshGeometry()
        status = succeeded ? "\(name) complete" : "\(name) failed: \(bridge.lastError)"
    }

    private func refreshGeometry() {
        vertexData = bridge.triangleVertexData as Data
        revision = UInt(bridge.revision)
    }
}
