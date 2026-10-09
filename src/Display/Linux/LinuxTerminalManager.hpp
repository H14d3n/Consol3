#ifndef LINUXTERMINALMANAGER_HPP
#define LINUXTERMINALMANAGER_HPP

#include "Display/ITerminalManager.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Display
{
    namespace Linux
    {
        /**
         * What the terminal reported it can do with images
         */
        struct TerminalGraphicsSupport
        {
            bool kitty_graphics = false;
            bool sixel          = false;
            // 0 if the terminal didn't report it
            uint16_t sixel_color_registers = 0;
            // 0 if the terminal didn't report it
            uint16_t cell_pixel_width  = 0;
            uint16_t cell_pixel_height = 0;

            // sixel with only a few colors looks worse than the half block characters, which have all of them
            static constexpr uint16_t MIN_SIXEL_COLOR_REGISTERS = 64;

            [[nodiscard]] bool HasUsableSixel() const
            {
                return sixel && (sixel_color_registers == 0 || sixel_color_registers >= MIN_SIXEL_COLOR_REGISTERS);
            }

            // terminals that don't say how many color registers they have usually have at least 256
            [[nodiscard]] uint16_t GetSixelPaletteSize() const
            {
                return sixel_color_registers == 0 ? 256 : std::min<uint16_t>(sixel_color_registers, 256);
            }
        };

        class LinuxTerminalManager : public ITerminalManager<char>
        {
        private:
            TerminalGraphicsSupport graphics_support;

            // asks the terminal and waits for the answers, has to happen before anything else reads the terminal input
            static TerminalGraphicsSupport QueryGraphicsSupport();

        public:
            LinuxTerminalManager();
            ~LinuxTerminalManager();

            // the size of the terminal can't be forced (most terminals ignore resize requests), so the framebuffer follows whatever the terminal currently is
            static bool QueryTerminalSize(uint16_t& width, uint16_t& height);

            [[nodiscard]] const TerminalGraphicsSupport& GetGraphicsSupport() const;

            virtual void SetupTerminalManager() override;

            virtual void SetPalette(const uint32_t palette[]) override;
            virtual void SetTitle(const std::string& title) override;

            virtual void DisableCursor() override;
            virtual void EnableCursor() override;

            virtual bool GetDrawableSize(uint16_t& width, uint16_t& height) const override;
            [[nodiscard]] virtual float GetCellAspectRatio() const override;
            virtual bool GetCellPixelSize(uint16_t& width, uint16_t& height) const override;

            virtual void WriteFrameBufferData(const char* data) override;
            virtual void WriteSizedString(const std::string& string, uint64_t size) override;
        };
    }

}

#endif
