#ifndef ITERMINALMANAGER_HPP
#define ITERMINALMANAGER_HPP

#include "RGBColor.hpp"

#include <stdint.h>
#include <string>

namespace Display
{
    template<typename T>
    class ITerminalManager
    {
    protected:
        ITerminalManager()
        {
        }

        virtual ~ITerminalManager() = default;

    public:
        virtual void SetupTerminalManager()               = 0;
        virtual void SetPalette(const uint32_t palette[]) = 0;
        virtual void SetTitle(const std::string& title)   = 0;

        virtual void DisableCursor() = 0;
        virtual void EnableCursor()  = 0;

        /**
         * Queries the current drawable size in cells, returns false if the size is fixed by the terminal manager or unknown
         */
        virtual bool GetDrawableSize(uint16_t& width, uint16_t& height) const
        {
            return false;
        }

        /**
         * Width / height of a single cell, terminal cells are usually about twice as tall as they are wide
         */
        [[nodiscard]] virtual float GetCellAspectRatio() const
        {
            return 1.0f;
        }

        /**
         * Size of a single cell in pixels, returns false if unknown
         */
        virtual bool GetCellPixelSize(uint16_t& width, uint16_t& height) const
        {
            return false;
        }

        virtual void WriteFrameBufferData(const T* data) = 0;
        /**
         * This call can contain ansi escape sequences, growing larger than the framebuffer size
         */
        virtual void WriteSizedString(const std::string& string, uint64_t size) = 0;
    };
}

#endif
