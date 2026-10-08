#ifndef VT24BITHALFBLOCKFRAMEDRAWER_HPP
#define VT24BITHALFBLOCKFRAMEDRAWER_HPP

#include "Display/FrameBuffer.hpp"
#include "Display/IFrameDrawer.hpp"
#include "Display/ITerminalManager.hpp"
#include "Display/RGBColor.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Display
{
    namespace Multiplatform
    {
        /**
         * Draws 2 pixels per terminal cell using the upper half block character: the foreground color is the top pixel and the background color the bottom one
         * Doubles the vertical resolution, and since terminal cells are about twice as tall as they are wide, the pixels end up roughly square
         * Needs a terminal with 24 bit color and UTF-8
         */
        template<typename T>
        class VT24BitHalfBlockFrameDrawer : public IFrameDrawer
        {
        private:
            // twice as tall as the terminal
            std::shared_ptr<FrameBuffer<uint32_t>> framebuffer;
            std::shared_ptr<ITerminalManager<T>> terminal_manager;

            std::string framebuffer_string;
            uint64_t framebuffer_string_len;

            // ▀ in UTF-8
            const std::string upper_half_block = "\xE2\x96\x80";

            // longest possible cell: \x1b[38;2;255;255;255;48;2;255;255;255m▀
            static constexpr uint64_t MAX_CELL_LEN = 39;
            // longest possible row position sequence: \x1b[65535;1H
            static constexpr uint64_t MAX_ROW_SEQUENCE_LEN = 11;

            void AllocateFrameBufferString();
            void WriteColor(uint64_t& string_index, const char* prefix, uint32_t color);
            void TranslateFrameBuffer();

        public:
            VT24BitHalfBlockFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager);

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
