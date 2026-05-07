// Aether Shaders — single source for all example and renderer shaders.
// Compiled via Aether.Shaders (Slang) → DXIL for D3D12 or SPIRV for Vulkan.

// ============================================================================
// Triangle — vertex format: position(float3) + color(float4)
// Used by: SlangTriangleExample (entry points: vertexMain, pixelMain)
// ============================================================================

struct VSOutput_Color4 {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

[shader("vertex")]
VSOutput_Color4 vertexMain(
    float3 position : POSITION,
    float4 color : COLOR)
{
    VSOutput_Color4 o;
    o.pos = float4(position, 1.0);
    o.color = color;
    return o;
}

[shader("pixel")]
float4 pixelMain(VSOutput_Color4 input) : SV_TARGET {
    return input.color;
}

// ============================================================================
// Triangle — vertex format: position(float3) + color(float3) + uv(float2)
// Used by: TriangleExample (entry points: mainVS, mainPS)
// ============================================================================

struct VSOutput_Color3 {
    float4 pos : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VSOutput_Color3 mainVS(
    float3 pos : POSITION,
    float3 color : COLOR,
    float2 uv : TEXCOORD)
{
    VSOutput_Color3 o;
    o.pos = float4(pos, 1.0);
    o.color = color;
    o.uv = uv;
    return o;
}

[shader("pixel")]
float4 mainPS(VSOutput_Color3 input) : SV_TARGET {
    return float4(input.color, 1.0);
}

// ============================================================================
// GPU Culling — frustum culling + LOD selection
// Used by: CullingJob (entry point: cullingCS)
// ============================================================================

struct SceneObject {
    float4x4 worldMatrix;
    float4   boundingSphere; // xyz=center, w=radius
    uint     meshIndex;
    uint     lodCount;
    float    lodDistances[4];
    uint2    _pad;
};

struct VisibleInfo {
    uint visible : 1;
    uint lodLevel : 3;
    uint _pad : 28;
};

struct ViewData {
    float4x4 viewProj;
    float3   eyePos;
    float    nearPlane;
    float    farPlane;
};

ConstantBuffer<ViewData> viewData : register(b0);
StructuredBuffer<SceneObject> sceneObjects : register(t0);
RWStructuredBuffer<VisibleInfo> visibleOutput : register(u0);

[shader("compute")]
[numthreads(64, 1, 1)]
void cullingCS(uint3 dispatchId : SV_DispatchThreadID) {
    uint objIndex = dispatchId.x;
    uint objectCount;
    uint stride;
    sceneObjects.GetDimensions(objectCount, stride);
    if (objIndex >= objectCount) return;

    SceneObject obj = sceneObjects[objIndex];
    VisibleInfo info = (VisibleInfo)0;

    // Frustum culling: check sphere against all 6 planes of view-proj
    float4 sphere = obj.boundingSphere;
    float4 c0 = viewData.viewProj[0];
    float4 c1 = viewData.viewProj[1];
    float4 c2 = viewData.viewProj[2];
    float4 c3 = viewData.viewProj[3];

    // Left: m3 + m0
    float d = c3.x + c0.x;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Right: m3 - m0
    d = c3.x - c0.x;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Bottom: m3 + m1
    d = c3.y + c0.y;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Top: m3 - m1
    d = c3.y - c0.y;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Near: m3 + m2
    d = c3.z + c0.z;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Far: m3 - m2
    d = c3.z - c0.z;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Sphere is visible — determine LOD
    float3 eyeToObj = sphere.xyz - viewData.eyePos;
    float dist = length(eyeToObj);

    uint lod = 0;
    for (uint i = 0; i < obj.lodCount && i < 4; ++i) {
        if (dist > obj.lodDistances[i]) lod = i + 1;
    }
    if (lod >= obj.lodCount) lod = obj.lodCount - 1;

    info.visible = 1;
    info.lodLevel = lod;
    visibleOutput[objIndex] = info;
}

// ============================================================================
// Visibility Compaction — reads culling output, writes packed indirect args
// Used by: IndirectDrawManager (entry point: compactCS)
// ============================================================================

struct DrawArg {
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int  baseVertexLocation;
    uint startInstanceLocation;
};

ConstantBuffer<uint> objectCount : register(b0);
StructuredBuffer<SceneObject> sceneObjectsCompact : register(t0);
StructuredBuffer<VisibleInfo> visibleInput : register(t1);
RWStructuredBuffer<DrawArg> indirectOutput : register(u0);

groupshared uint compactCounter;

[shader("compute")]
[numthreads(64, 1, 1)]
void compactCS(uint3 dispatchId : SV_DispatchThreadID) {
    uint objIndex = dispatchId.x;
    if (objIndex == 0) compactCounter = 0;
    GroupMemoryBarrierWithGroupSync();

    if (objIndex >= objectCount) return;

    VisibleInfo info = visibleInput[objIndex];
    if (info.visible) {
        uint destIndex;
        InterlockedAdd(compactCounter, 1, destIndex);

        SceneObject obj = sceneObjectsCompact[objIndex];
        DrawArg arg;
        arg.indexCountPerInstance = 6; // per mesh; TODO: use mesh data
        arg.instanceCount = 1;
        arg.startIndexLocation = 0;
        arg.baseVertexLocation = 0;
        arg.startInstanceLocation = objIndex;
        indirectOutput[destIndex] = arg;
    }
}

// ============================================================================
// Forward rendering — vertex format: position(float3) only
// Used by: SceneManager forward rendering (forwardVS, forwardPS)
// ============================================================================

struct VSOutput_Forward {
    float4 pos : SV_POSITION;
};

[shader("vertex")]
VSOutput_Forward forwardVS(float3 position : POSITION) {
    VSOutput_Forward o;
    o.pos = float4(position, 1.0);
    return o;
}

[shader("pixel")]
float4 forwardPS(VSOutput_Forward input) : SV_TARGET {
    return float4(0.0, 1.0, 0.0, 1.0);
}
