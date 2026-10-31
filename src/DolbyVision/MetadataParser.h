#pragma once
#include <libdovi/rpu_parser.h>
#include <array>
#include <cstring>

// RPU parsing only. No HEVC pictures are decoded by this helper.
class DoviMetadataParser
{
    std::array<AVDOVIDataMapping, 16> mappings{};
    std::array<bool, 16> valid{};

    static int64_t Fixed(int64_t integer, uint64_t fraction, unsigned bits)
    {
        const int64_t scale = 1LL << bits;
        check(fraction < uint64_t(scale) && integer >= INT64_MIN / scale &&
                  integer <= (INT64_MAX - int64_t(fraction)) / scale,
              "RPU fixed coefficient bounds");
        return integer * scale + int64_t(fraction);
    }

    template <typename T> static const T& At(const T* data, size_t size, size_t index)
    {
        check(data && index < size, "RPU array bounds");
        return data[index];
    }

    static void Curve(AVDOVIReshapingCurve& out, const DoviReshapingCurve& in, unsigned bits)
    {
        check(in.num_pivots_minus2 <= 7 && in.pivots.len == in.num_pivots_minus2 + 2, "RPU pivot count");
        out.num_pivots = uint8_t(in.pivots.len);
        uint32_t pivot = 0;
        for (unsigned i = 0; i < out.num_pivots; ++i)
        {
            // libdovi retains the bitstream's delta-coded pivot values.
            pivot += At(in.pivots.data, in.pivots.len, i);
            check(pivot <= UINT16_MAX, "RPU pivot bounds");
            out.pivots[i] = uint16_t(pivot);
        }
        for (unsigned i = 0; i + 1 < out.num_pivots; ++i)
        {
            check(in.mapping_idc <= 1, "RPU mapping method");
            out.mapping_idc[i] = AVDOVIMappingMethod(in.mapping_idc);
            if (!in.mapping_idc)
            {
                check(in.polynomial, "RPU polynomial");
                const auto& poly = *in.polynomial;
                auto order = At(poly.poly_order_minus1.data, poly.poly_order_minus1.len, i) + 1;
                check(order <= 2 && !At(poly.linear_interp_flag.data, poly.linear_interp_flag.len, i),
                      "RPU polynomial order/interpolation");
                out.poly_order[i] = uint8_t(order);
                auto ip = At(poly.poly_coef_int.list, poly.poly_coef_int.len, i);
                auto fp = At(poly.poly_coef.list, poly.poly_coef.len, i);
                check(ip && fp, "RPU polynomial coefficient arrays");
                for (unsigned k = 0; k <= order; ++k)
                    out.poly_coef[i][k] = Fixed(At(ip->data, ip->len, k), At(fp->data, fp->len, k), bits);
            }
            else
            {
                check(in.mmr, "RPU MMR");
                const auto& mmr = *in.mmr;
                auto order = unsigned(At(mmr.mmr_order_minus1.data, mmr.mmr_order_minus1.len, i)) + 1;
                check(order <= 3, "RPU MMR order");
                out.mmr_order[i] = uint8_t(order);
                out.mmr_constant[i] = Fixed(At(mmr.mmr_constant_int.data, mmr.mmr_constant_int.len, i),
                                            At(mmr.mmr_constant.data, mmr.mmr_constant.len, i),
                                            bits);
                auto ip = At(mmr.mmr_coef_int.list, mmr.mmr_coef_int.len, i);
                auto fp = At(mmr.mmr_coef.list, mmr.mmr_coef.len, i);
                check(ip && fp, "RPU MMR coefficient arrays");
                for (unsigned j = 0; j < order; ++j)
                {
                    auto integers = At(ip->list, ip->len, j);
                    auto fractions = At(fp->list, fp->len, j);
                    check(integers && fractions, "RPU MMR order arrays");
                    for (unsigned k = 0; k < 7; ++k)
                        out.mmr_coef[i][j][k] =
                            Fixed(At(integers->data, integers->len, k), At(fractions->data, fractions->len, k), bits);
                }
            }
        }
    }

  public:
    void Reset()
    {
        valid.fill(false);
    }

  private:
    using Rpu = std::unique_ptr<DoviRpuOpaque, decltype(&dovi_rpu_free)>;
    using Header = std::unique_ptr<const DoviRpuDataHeader, decltype(&dovi_rpu_free_header)>;

    static Rpu Parse(const uint8_t* nal, size_t size)
    {
        Rpu rpu(dovi_parse_unspec62_nalu(nal, size), dovi_rpu_free);
        check(rpu && !dovi_rpu_get_error(rpu.get()), "valid RPU bitstream");
        return rpu;
    }

  public:
    // Null means genuine residual processing is required; keep the existing FEL decoder path.
    AVFrame* ParseMel(const uint8_t* nal, size_t size)
    {
        auto rpu = Parse(nal, size);
        Header header(dovi_rpu_get_header(rpu.get()), dovi_rpu_free_header);
        check(header && header->guessed_profile == 7 && header->el_type, "Profile 7 RPU header");
        if (strcmp(header->el_type, "MEL") != 0)
            return nullptr;
        return Convert(rpu.get(), *header);
    }

    // Profiles 5 and 8 carry a single layer; their RPU travels in the base-layer stream.
    AVFrame* ParseSingleLayer(const uint8_t* nal, size_t size)
    {
        auto rpu = Parse(nal, size);
        Header header(dovi_rpu_get_header(rpu.get()), dovi_rpu_free_header);
        check(header && (header->guessed_profile == 5 || header->guessed_profile == 8) && header->disable_residual_flag,
              "single-layer Profile 5/8 RPU header");
        return Convert(rpu.get(), *header);
    }

  private:
    AVFrame* Convert(DoviRpuOpaque* rpu, const DoviRpuDataHeader& headerData)
    {
        const auto* header = &headerData;
        check(header->coefficient_data_type == 0 && header->coefficient_log2_denom <= 32 &&
                  header->bl_bit_depth_minus8 <= 8 && header->el_bit_depth_minus8 <= 8 &&
                  header->vdr_bit_depth_minus8 <= 8,
              "supported RPU coefficient representation");
        size_t bytes = 0;
        std::unique_ptr<AVDOVIMetadata, decltype(&av_free)> metadata(av_dovi_metadata_alloc(&bytes), av_free);
        check(metadata != nullptr, "RPU metadata allocation");
        auto h = av_dovi_get_header(metadata.get());
        h->rpu_type = header->rpu_type;
        h->rpu_format = header->rpu_format;
        h->vdr_rpu_profile = header->vdr_rpu_profile;
        h->vdr_rpu_level = header->vdr_rpu_level;
        h->chroma_resampling_explicit_filter_flag = header->chroma_resampling_explicit_filter_flag;
        h->coef_data_type = header->coefficient_data_type;
        h->coef_log2_denom = uint8_t(header->coefficient_log2_denom);
        h->vdr_rpu_normalized_idc = header->vdr_rpu_normalized_idc;
        h->bl_video_full_range_flag = header->bl_video_full_range_flag;
        h->bl_bit_depth = uint8_t(header->bl_bit_depth_minus8 + 8);
        h->el_bit_depth = uint8_t(header->el_bit_depth_minus8 + 8);
        h->vdr_bit_depth = uint8_t(header->vdr_bit_depth_minus8 + 8);
        h->spatial_resampling_filter_flag = header->spatial_resampling_filter_flag;
        h->el_spatial_resampling_filter_flag = header->el_spatial_resampling_filter_flag;
        h->disable_residual_flag = header->disable_residual_flag;
        auto mapping = av_dovi_get_mapping(metadata.get());
        if (header->use_prev_vdr_rpu_flag)
        {
            check(header->prev_vdr_rpu_id < valid.size() && valid[header->prev_vdr_rpu_id],
                  "previous RPU mapping available");
            *mapping = mappings[header->prev_vdr_rpu_id];
        }
        else
        {
            std::unique_ptr<const DoviRpuDataMapping, decltype(&dovi_rpu_free_data_mapping)> data(
                dovi_rpu_get_data_mapping(rpu), dovi_rpu_free_data_mapping);
            check(data && data->vdr_rpu_id < valid.size(), "RPU mapping id");
            mapping->vdr_rpu_id = uint8_t(data->vdr_rpu_id);
            mapping->mapping_color_space = uint8_t(data->mapping_color_space);
            mapping->mapping_chroma_format_idc = uint8_t(data->mapping_chroma_format_idc);
            mapping->num_x_partitions = uint32_t(data->num_x_partitions_minus1 + 1);
            mapping->num_y_partitions = uint32_t(data->num_y_partitions_minus1 + 1);
            mapping->nlq_method_idc = AVDOVINLQMethod(data->nlq_method_idc);
            for (int c = 0; c < 3; ++c)
            {
                Curve(mapping->curves[c], data->curves[c], h->coef_log2_denom);
                if (data->nlq)
                {
                    const auto& nlq = *data->nlq;
                    auto& out = mapping->nlq[c];
                    out.nlq_offset = nlq.nlq_offset[c];
                    out.vdr_in_max = Fixed(int64_t(nlq.vdr_in_max_int[c]), nlq.vdr_in_max[c], h->coef_log2_denom);
                    out.linear_deadzone_slope = Fixed(
                        int64_t(nlq.linear_deadzone_slope_int[c]), nlq.linear_deadzone_slope[c], h->coef_log2_denom);
                    out.linear_deadzone_threshold = Fixed(int64_t(nlq.linear_deadzone_threshold_int[c]),
                                                          nlq.linear_deadzone_threshold[c],
                                                          h->coef_log2_denom);
                }
            }
            mappings[mapping->vdr_rpu_id] = *mapping;
            valid[mapping->vdr_rpu_id] = true;
        }
        std::unique_ptr<const DoviVdrDmData, decltype(&dovi_rpu_free_vdr_dm_data)> dm(dovi_rpu_get_vdr_dm_data(rpu),
                                                                                      dovi_rpu_free_vdr_dm_data);
        check(dm && !dm->compressed && dm->affected_dm_metadata_id == dm->current_dm_metadata_id,
              "complete uncompressed RPU color metadata");
        auto color = av_dovi_get_color(metadata.get());
        color->dm_metadata_id = uint8_t(dm->affected_dm_metadata_id);
        color->scene_refresh_flag = uint8_t(dm->scene_refresh_flag);
        const int16_t ycc[] = {dm->ycc_to_rgb_coef0,
                               dm->ycc_to_rgb_coef1,
                               dm->ycc_to_rgb_coef2,
                               dm->ycc_to_rgb_coef3,
                               dm->ycc_to_rgb_coef4,
                               dm->ycc_to_rgb_coef5,
                               dm->ycc_to_rgb_coef6,
                               dm->ycc_to_rgb_coef7,
                               dm->ycc_to_rgb_coef8};
        const int16_t lms[] = {dm->rgb_to_lms_coef0,
                               dm->rgb_to_lms_coef1,
                               dm->rgb_to_lms_coef2,
                               dm->rgb_to_lms_coef3,
                               dm->rgb_to_lms_coef4,
                               dm->rgb_to_lms_coef5,
                               dm->rgb_to_lms_coef6,
                               dm->rgb_to_lms_coef7,
                               dm->rgb_to_lms_coef8};
        for (int i = 0; i < 9; ++i)
        {
            color->ycc_to_rgb_matrix[i] = {ycc[i], 1 << 13};
            color->rgb_to_lms_matrix[i] = {lms[i], 1 << 14};
        }
        const uint32_t offsets[] = {dm->ycc_to_rgb_offset0, dm->ycc_to_rgb_offset1, dm->ycc_to_rgb_offset2};
        for (int i = 0; i < 3; ++i)
        {
            unsigned offset = offsets[i], denominator = 1 << 28;
            if (offset > INT_MAX)
            {
                offset >>= 1;
                denominator >>= 1;
            }
            color->ycc_to_rgb_offset[i] = {int(offset), int(denominator)};
        }
        color->signal_eotf = dm->signal_eotf;
        color->signal_eotf_param0 = dm->signal_eotf_param0;
        color->signal_eotf_param1 = dm->signal_eotf_param1;
        color->signal_eotf_param2 = dm->signal_eotf_param2;
        color->signal_bit_depth = dm->signal_bit_depth;
        color->signal_color_space = dm->signal_color_space;
        color->signal_chroma_format = dm->signal_chroma_format;
        color->signal_full_range_flag = dm->signal_full_range_flag;
        color->source_min_pq = dm->source_min_pq;
        color->source_max_pq = dm->source_max_pq;
        color->source_diagonal = dm->source_diagonal;
        if (dm->dm_data.level1)
        {
            metadata->num_ext_blocks = 1;
            auto block = av_dovi_get_ext(metadata.get(), 0);
            block->level = 1;
            block->l1 = {dm->dm_data.level1->min_pq, dm->dm_data.level1->max_pq, dm->dm_data.level1->avg_pq};
        }
        std::unique_ptr<AVFrame, void (*)(AVFrame*)> frame(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
        check(frame != nullptr, "RPU metadata carrier");
        auto sd = av_frame_new_side_data(frame.get(), AV_FRAME_DATA_DOVI_METADATA, bytes);
        check(sd != nullptr, "RPU frame metadata");
        memcpy(sd->data, metadata.get(), bytes);
        pl_dovi_metadata mapped = {};
        pl_map_dovi_metadata(&mapped, metadata.get());
        check(!mapped.nlq_active, "metadata must not require residual processing");
        return frame.release();
    }
};
