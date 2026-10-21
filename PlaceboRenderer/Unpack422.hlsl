Texture2D<float4> packedInput : register(t0);
RWTexture2D<float> outputY : register(u0);
RWTexture2D<float2> outputUV : register(u1);
[numthreads(16,16,1)]
void unpack422(uint3 id : SV_DispatchThreadID){
    uint width,height;outputUV.GetDimensions(width,height);
    if(id.x>=width||id.y>=height)return;
    float4 value=packedInput.Load(int3(id.xy,0));
    outputY[uint2(id.x*2,id.y)]=value.r;
    outputY[uint2(id.x*2+1,id.y)]=value.b;
    outputUV[id.xy]=value.ga;
}
