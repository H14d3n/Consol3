#ifndef SIXELFRAMEDRAWER_HPP
#define SIXELFRAMEDRAWER_HPP

#include "Display/ColorQuantizer.hpp"
#include "Display/FrameBuffer.hpp"
#include "Display/IFrameDrawer.hpp"
#include "Display/ITerminalManager.hpp"
#include "Display/RGBColor.hpp"
#include "GraphicsFrameSize.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Display
{
    namespace Multiplatform
    {
        /**
         * Sends every frame as a sixel image, which shows real pixels instead of characters, so the resolution only depends on the size of the terminal window
         * Sixel images use a palette, every frame gets its own palette of up to 256 colors
         * Needs a terminal with sixel support (e.g. Windows Terminal, WezTerm, foot, Konsole, mlterm, xterm -ti vt340)
         */
        template<typename T>
        class SixelFrameDrawer : public IFrameDrawer
        {
        private:
            std::shared_ptr<FrameBuffer<uint32_t>> framebuffer;
            std::shared_ptr<ITerminalManager<T>> terminal_manager;

            uint16_t palette_size;
            uint32_t max_pixels;
            GraphicsFrameSize frame_size;

            ColorQuantizer quantizer;
            std::vector<uint8_t> indexed_framebuffer;

            // the sixel bits of every palette color for the band of 6 rows being encoded
            std::vector<uint8_t> band_bits;
            std::vector<uint8_t> band_colors;
            std::vector<bool> band_color_used;
            // the columns each color covers in the band, only those are encoded
            std::vector<uint16_t> band_color_begin;
            std::vector<uint16_t> band_color_end;

            std::string sixel_string;

            void ResizeBuffers();
            void AppendNumber(uint32_t number);
            void AppendSixelRun(char sixel, uint16_t count);
            void EncodeBand(uint16_t band_y);
            void TranslateFrameBuffer();

        public:
            SixelFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager, uint16_t palette_size = 256, uint32_t max_pixels = 250000);

            virtual void SetupFrameDrawer() override;

            virtual void SetPixel(uint16_t x, uint16_t y, RGBColor color) override;

            virtual void DisplayFrame() override;

            virtual void ClearFrameBuffer() override;

            virtual void ReportInformation(const std::string& info) override;

            [[nodiscard]] virtual const uint16_t GetFrameBufferWidth() const override;
            [[nodiscard]] virtual const uint16_t GetFrameBufferHeight() const override;

            virtual bool UpdateFrameBufferSize() override;
            [[nodiscard]] virtual float GetPixelAspectRatio() const override;
        };
    }
}

#endif
