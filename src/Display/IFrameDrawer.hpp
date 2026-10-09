#ifndef IFRAMEDRAWER_HPP
#define IFRAMEDRAWER_HPP

#include "HSVColor.hpp"

// Windows.h overrides std::min
#define NOMINMAX
#include <cstdint>
#include <string>

namespace Display
{
    class IFrameDrawer
    {
    protected:
        IFrameDrawer()
        {
        }

        virtual ~IFrameDrawer() = default;

    public:
        virtual void SetupFrameDrawer()                               = 0;
        virtual void SetPixel(uint16_t x, uint16_t y, RGBColor color) = 0;

        /**
         * Called when the frame drawer stops being used, removes anything it left on the display that the next one wouldn't overwrite
         */
        virtual void ReleaseFrameDrawer()
        {
        }

        virtual void ClearFrameBuffer() = 0;
        virtual void DisplayFrame()     = 0;

        virtual void ReportInformation(const std::string& info) = 0;

        [[nodiscard]] virtual const uint16_t GetFrameBufferWidth() const  = 0;
        [[nodiscard]] virtual const uint16_t GetFrameBufferHeight() const = 0;

        /**
         * Matches the framebuffer to the current size of the display, returns true if the size changed
         */
        virtual bool UpdateFrameBufferSize()
        {
            return false;
        }

        /**
         * Width / height of a single displayed pixel
         */
        [[nodiscard]] virtual float GetPixelAspectRatio() const
        {
            return 1.0f;
        }
    };
}

#endif
