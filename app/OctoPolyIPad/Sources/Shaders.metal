#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vertex_main(const device packed_float3 *vertices [[buffer(0)]],
                             uint vertexId [[vertex_id]]) {
    const float3 point = float3(vertices[vertexId]);
    VertexOut out;
    out.position = float4(point.x * 0.32 - point.z * 0.12,
                          point.y * 0.32 + point.z * 0.08,
                          0.0,
                          1.0);
    out.color = float3(0.20 + 0.07 * float(vertexId % 3),
                       0.58,
                       0.92 - 0.08 * float(vertexId % 3));
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
