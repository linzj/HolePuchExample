cbuffer ColorBuffer : register(b0)
{
    float4 color;
}

struct VS_Input {
    float2 pos : POS;
    float2 uv : TEX;
};

struct VS_Output {
    float4 pos : SV_POSITION;
};


VS_Output vs_main(VS_Input input)
{
    VS_Output output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    return output;
}

float4 ps_main(VS_Output input) : SV_Target
{
  return color;
}
