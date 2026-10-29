#include "Renderer/MediaFormat.cpp"
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* message)
{
    if (!ok)
    {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

int main()
{
    for (unsigned depth : {8u, 10u, 12u, 16u})
    {
        MediaSideDataDOVIMetadata metadata = {};
        metadata.Header.disable_residual_flag = 1;
        metadata.Header.bl_bit_depth = static_cast<uint8_t>(depth);
        metadata.Header.coef_log2_denom = 20;
        metadata.ColorMetadata.source_max_pq = 3079;
        for (int i = 0; i < 3; ++i)
        {
            metadata.ColorMetadata.ycc_to_rgb_matrix[i * 3 + i] = 1;
            metadata.ColorMetadata.rgb_to_lms_matrix[i * 3 + i] = 1;
            auto& curve = metadata.Mapping.curves[i];
            curve.num_pivots = 3;
            curve.pivots[1] = uint16_t(1u << (depth - 1));
            curve.pivots[2] = uint16_t((1u << depth) - 1);
            curve.poly_order[0] = curve.poly_order[1] = 1;
            curve.poly_coef[0][1] = curve.poly_coef[1][1] = 1 << 20;
        }
        VideoFrame frame;
        frame.format = PixelFormat::P010;
        Require(MapDovi(reinterpret_cast<const BYTE*>(&metadata), sizeof(metadata), true, frame), "DV mapping");
        for (const auto& curve : frame.dovi->comp)
        {
            Require(curve.pivots[0] == 0 && curve.pivots[1] == .5f, "ETSI pivot code units");
            Require(curve.pivots[2] == float((1u << depth) - 1) / float(1u << depth), "maximum pivot below one");
            Require(curve.poly_coeffs[0][1] == 1 && curve.poly_coeffs[1][1] == 1, "coefficient normalization");
        }
        Require(!frame.dovi->nlq_active, "single-layer metadata does not request missing FEL");
        metadata.Header.disable_residual_flag = 0;
        Require(!MapDovi(reinterpret_cast<const BYTE*>(&metadata), sizeof(metadata), true, frame),
                "FEL remains rejected without decoded enhancement layer");
        metadata.Header.disable_residual_flag = 1;
        metadata.Mapping.curves[0].pivots[1] = metadata.Mapping.curves[0].pivots[2];
        Require(!MapDovi(reinterpret_cast<const BYTE*>(&metadata), sizeof(metadata), true, frame),
                "invalid pivot ordering rejected");
    }
    puts(
        "PASS Dolby Vision: 8/10/12/16-bit ETSI pivot units, coefficients, no false FEL enable, invalid input rejection");
}
