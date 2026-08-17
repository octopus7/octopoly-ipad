import MetalKit

final class MeshRenderer: NSObject, MTKViewDelegate {
    private var commandQueue: MTLCommandQueue?
    private var pipelineState: MTLRenderPipelineState?
    private var vertexBuffer: MTLBuffer?
    private var vertexCount = 0

    func attach(to view: MTKView) {
        guard let device = view.device,
              let library = device.makeDefaultLibrary(),
              let vertexFunction = library.makeFunction(name: "vertex_main"),
              let fragmentFunction = library.makeFunction(name: "fragment_main") else {
            return
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertexFunction
        descriptor.fragmentFunction = fragmentFunction
        descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
        pipelineState = try? device.makeRenderPipelineState(descriptor: descriptor)
        commandQueue = device.makeCommandQueue()
        view.delegate = self
    }

    func update(vertexData: Data) {
        vertexCount = vertexData.count / (MemoryLayout<Float>.stride * 3)
        guard let device = commandQueue?.device, !vertexData.isEmpty else {
            vertexBuffer = nil
            return
        }
        vertexBuffer = vertexData.withUnsafeBytes { bytes in
            guard let baseAddress = bytes.baseAddress else { return nil }
            return device.makeBuffer(bytes: baseAddress,
                                     length: vertexData.count,
                                     options: .storageModeShared)
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard vertexCount >= 3,
              let pipelineState,
              let vertexBuffer,
              let descriptor = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let commandBuffer = commandQueue?.makeCommandBuffer(),
              let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: descriptor) else {
            return
        }
        encoder.setRenderPipelineState(pipelineState)
        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: vertexCount)
        encoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }
}
