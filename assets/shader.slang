struct VSInput {
    float3 Pos;
    float3 Normal;
    float2 UV;
};

Sampler2D textures[];

struct ObjectData {
    float4x4 model;
    uint32_t texture_index;
    uint32_t selected;
};

struct PushConstants {
    ShaderData* shader_data_addrress;
    float time;
};

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4 lightPos;
    ObjectData objects[16276];
};

struct VSOutput {
    float4 Pos : SV_POSITION;
    float3 Normal;
    float2 UV;
    float3 Factor;
    float3 LightVec;
    float3 ViewVec;
    uint32_t InstanceIndex;
    uint32_t TextureIndex;
};

[shader("vertex")]
VSOutput main(
    VSInput input,
    uniform PushConstants pushConstants,
    uint instanceIndex : SV_VulkanInstanceID)
{
    VSOutput output;
    ShaderData* shaderData = (ShaderData*)pushConstants.shader_data_addrress;

    ObjectData obj = shaderData->objects[instanceIndex];
    float4x4 modelMat = obj.model;
    output.TextureIndex = obj.texture_index;

    float4x4 viewModel = mul(shaderData->view, modelMat);

    output.Normal = mul((float3x3)viewModel, input.Normal);
    output.UV = input.UV;

    output.Pos = mul(
        shaderData->projection,
        mul(viewModel, float4(input.Pos, 1.0))
    );

    output.Factor = obj.selected != 0 ? 3.0 : 1.0;
    output.InstanceIndex = instanceIndex;

    float3 fragPos = mul(viewModel, float4(input.Pos, 1.0)).xyz;
    float3 lightPos = mul(shaderData->view, shaderData->lightPos).xyz;

    output.LightVec = lightPos - fragPos;
    output.ViewVec = -fragPos;

    return output;
}

[shader("fragment")]
float4 main(VSOutput input) {
    // Phong lighting
    float3 N = normalize(input.Normal);
    float3 L = normalize(input.LightVec);
    float3 V = normalize(input.ViewVec);
    float3 R = reflect(-L, N);

    float3 ambient = 0.1;

    float3 diffuse = max(dot(N, L), 0.0025);
    // Sample from texture
    float3 color = textures[NonUniformResourceIndex(input.TextureIndex)].Sample(input.UV).rgb * input.Factor;
    return float4((ambient + diffuse) * color.rgb, 1.0);
}