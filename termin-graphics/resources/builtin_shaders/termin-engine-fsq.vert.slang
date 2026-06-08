struct VertexInput
{
    [[vk::location(0)]]
    float2 position : POSITION;

    [[vk::location(1)]]
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
