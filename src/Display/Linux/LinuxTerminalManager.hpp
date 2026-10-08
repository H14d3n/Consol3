#ifndef LINUXTERMINALMANAGER_HPP
#define LINUXTERMINALMANAGER_HPP

#include "Display/ITerminalManager.hpp"

#include <cstdint>
#include <string>

namespace Display
{
    namespace Linux
    {
        class LinuxTerminalManager : public ITerminalManager<char>
        {
        public:
            LinuxTerminalManager();
            ~LinuxTerminalManager();

            // the size of the terminal can't be forced (most terminals ignore resize requests), so the framebuffer follows whatever the terminal currently is
            static bool QueryTerminalSize(uint16_t& width, uint16_t& height);

            virtual void SetupTerminalManager() override;

            virtual void SetPalette(const uint32_t palette[]) override;
            virtual void SetTitle(const std::string& title) override;

            virtual void DisableCursor() override;
            virtual void EnableCursor() override;

            virtual bool GetDrawableSize(uint16_t& width, uint16_t& height) const override;
            [[nodiscard]] virtual float GetCellAspectRatio() const override;

            virtual void WriteFrameBufferData(const char* data) override;
            virtual void WriteSizedString(const std::string& string, uint64_t size) override;
        };
    }

}

#endif
