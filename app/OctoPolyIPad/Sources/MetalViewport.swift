import MetalKit
import SwiftUI

struct MetalViewport: UIViewRepresentable {
    @ObservedObject var model: MeshViewModel

    func makeCoordinator() -> MeshRenderer {
        MeshRenderer()
    }

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.colorPixelFormat = .bgra8Unorm
        view.clearColor = MTLClearColor(red: 0.035, green: 0.045, blue: 0.065, alpha: 1)
        view.preferredFramesPerSecond = 30
        view.enableSetNeedsDisplay = true
        view.isPaused = true
        context.coordinator.attach(to: view)
        context.coordinator.update(vertexData: model.vertexData)
        return view
    }

    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.update(vertexData: model.vertexData)
        view.setNeedsDisplay()
    }
}
