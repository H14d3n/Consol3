#include "ColorQuantizer.hpp"

#include <algorithm>

namespace Display
{
    ColorQuantizer::ColorQuantizer() :
        bin_counts(BIN_COUNT, 0),
        bin_red_sums(BIN_COUNT, 0),
        bin_green_sums(BIN_COUNT, 0),
        bin_blue_sums(BIN_COUNT, 0),
        bin_palette_index(BIN_COUNT, 0)
    {
    }

    inline uint8_t ColorQuantizer::GetBinChannel(uint16_t key, uint8_t channel)
    {
        return (key >> (10 - channel * 5)) & 0x1F;
    }

    void ColorQuantizer::MeasureBox(Box& box) const
    {
        uint8_t min_channel[3] = {31, 31, 31};
        uint8_t max_channel[3] = {0, 0, 0};
        box.population         = 0;

        for (uint32_t i = box.begin; i < box.end; i++)
        {
            box.population += bins[i].count;

            for (uint8_t channel = 0; channel < 3; channel++)
            {
                uint8_t value        = GetBinChannel(bins[i].key, channel);
                min_channel[channel] = std::min(min_channel[channel], value);
                max_channel[channel] = std::max(max_channel[channel], value);
            }
        }

        box.longest_channel = 0;
        box.longest_length  = 0;
        for (uint8_t channel = 0; channel < 3; channel++)
        {
            uint8_t length = max_channel[channel] - min_channel[channel];
            if (length > box.longest_length)
            {
                box.longest_channel = channel;
                box.longest_length  = length;
            }
        }
    }

    void ColorQuantizer::SplitBox(size_t box_index)
    {
        Box box         = boxes[box_index];
        uint8_t channel = box.longest_channel;

        std::sort(bins.begin() + box.begin, bins.begin() + box.end, [channel](const Bin& a, const Bin& b) { return GetBinChannel(a.key, channel) < GetBinChannel(b.key, channel); });

        // split where half of the pixels are on each side, both sides need at least one bin
        uint64_t half_population = box.population / 2;
        uint64_t population      = 0;
        uint32_t split           = box.begin;
        while (split < box.end - 1 && population + bins[split].count <= half_population)
            population += bins[split++].count;
        split = std::clamp(split, box.begin + 1, box.end - 1);

        Box low  = {box.begin, split, 0, 0, 0};
        Box high = {split, box.end, 0, 0, 0};
        MeasureBox(low);
        MeasureBox(high);

        boxes[box_index] = low;
        boxes.push_back(high);
    }

    void ColorQuantizer::BuildPalette(const FrameBuffer<uint32_t>& framebuffer, uint16_t max_colors)
    {
        max_colors = std::clamp<uint16_t>(max_colors, 1, 256);

        for (const Bin& bin : bins)
        {
            bin_counts[bin.key]     = 0;
            bin_red_sums[bin.key]   = 0;
            bin_green_sums[bin.key] = 0;
            bin_blue_sums[bin.key]  = 0;
        }
        bins.clear();

        const uint32_t* pixels = framebuffer.GetFrameBufferData();
        uint32_t pixel_count   = static_cast<uint32_t>(framebuffer.GetWidth()) * framebuffer.GetHeight();

        for (uint32_t i = 0; i < pixel_count; i++)
        {
            uint32_t color = pixels[i];
            uint16_t key   = GetBinKey(color);

            if (bin_counts[key]++ == 0)
                bins.push_back({key, 0});

            bin_red_sums[key] += (color >> 16) & 0xFF;
            bin_green_sums[key] += (color >> 8) & 0xFF;
            bin_blue_sums[key] += color & 0xFF;
        }

        for (Bin& bin : bins)
            bin.count = bin_counts[bin.key];

        boxes.clear();
        palette.clear();
        if (bins.empty())
            return;

        Box all_bins = {0, static_cast<uint32_t>(bins.size()), 0, 0, 0};
        MeasureBox(all_bins);
        boxes.push_back(all_bins);

        while (boxes.size() < max_colors)
        {
            // split the box covering the most pixels over the widest range of colors
            size_t best_box     = boxes.size();
            uint64_t best_score = 0;
            for (size_t i = 0; i < boxes.size(); i++)
            {
                if (boxes[i].end - boxes[i].begin < 2)
                    continue;

                uint64_t score = boxes[i].population * boxes[i].longest_length;
                if (best_box == boxes.size() || score > best_score)
                {
                    best_box   = i;
                    best_score = score;
                }
            }

            if (best_box == boxes.size())
                break;

            SplitBox(best_box);
        }

        // every color of the palette is the average of the pixels in its box
        for (size_t box_index = 0; box_index < boxes.size(); box_index++)
        {
            const Box& box = boxes[box_index];

            uint64_t red   = 0;
            uint64_t green = 0;
            uint64_t blue  = 0;
            for (uint32_t i = box.begin; i < box.end; i++)
            {
                uint16_t key = bins[i].key;
                red += bin_red_sums[key];
                green += bin_green_sums[key];
                blue += bin_blue_sums[key];
                bin_palette_index[key] = static_cast<uint8_t>(box_index);
            }

            palette.emplace_back(static_cast<uint8_t>(red / box.population), static_cast<uint8_t>(green / box.population), static_cast<uint8_t>(blue / box.population));
        }
    }

    const std::vector<RGBColor>& ColorQuantizer::GetPalette() const
    {
        return palette;
    }
}
