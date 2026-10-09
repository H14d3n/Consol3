#ifndef COLORQUANTIZER_HPP
#define COLORQUANTIZER_HPP

#include "Display/FrameBuffer.hpp"
#include "Display/RGBColor.hpp"

#include <cstdint>
#include <vector>

namespace Display
{
    /**
     * Picks a palette of at most 256 colors that fits a frame, using median cut
     * Colors are grouped in a 15 bit (5 bits per channel) histogram, every group ends up in exactly one palette color
     */
    class ColorQuantizer
    {
    private:
        static constexpr uint32_t BIN_COUNT = 1 << 15;

        struct Bin
        {
            uint16_t key;
            uint32_t count;
        };

        struct Box
        {
            uint32_t begin;
            uint32_t end;
            uint64_t population;
            uint8_t longest_channel;
            uint8_t longest_length;
        };

        std::vector<uint32_t> bin_counts;
        std::vector<uint32_t> bin_red_sums;
        std::vector<uint32_t> bin_green_sums;
        std::vector<uint32_t> bin_blue_sums;
        std::vector<uint8_t> bin_palette_index;

        std::vector<Bin> bins;
        std::vector<Box> boxes;
        std::vector<RGBColor> palette;

        [[nodiscard]] static inline uint16_t GetBinKey(uint32_t color);
        [[nodiscard]] static inline uint8_t GetBinChannel(uint16_t key, uint8_t channel);

        void MeasureBox(Box& box) const;
        void SplitBox(size_t box_index);

    public:
        ColorQuantizer();

        void BuildPalette(const FrameBuffer<uint32_t>& framebuffer, uint16_t max_colors);

        [[nodiscard]] const std::vector<RGBColor>& GetPalette() const;

        [[nodiscard]] inline uint8_t GetPaletteIndex(uint32_t color) const
        {
            return bin_palette_index[GetBinKey(color)];
        }
    };

    inline uint16_t ColorQuantizer::GetBinKey(uint32_t color)
    {
        return static_cast<uint16_t>(((color >> 9) & 0x7C00) | ((color >> 6) & 0x03E0) | ((color >> 3) & 0x001F));
    }
}

#endif
