// triangle.slang — Slang shader for the triangle example
// Compiles to DXIL via Aether.Shaders with Slang backend.

struct VSOutput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(
    float3 position : POSITION,
    float4 color : COLOR)
{
    VSOutput output;
    output.pos = float4(position, 1.0);
    output.color = color;
    return output;
}

[shader("pixel")]
float4 pixelMain(VSOutput input) : SV_TARGET {
    return input.color;
}
