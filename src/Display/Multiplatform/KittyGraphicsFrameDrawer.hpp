#ifndef KITTYGRAPHICSFRAMEDRAWER_HPP
#define KITTYGRAPHICSFRAMEDRAWER_HPP

#include "Display/FrameBuffer.hpp"
#include "Display/IFrameDrawer.hpp"
#include "Display/ITerminalManager.hpp"
#include "Display/RGBColor.hpp"
#include "GraphicsFrameSize.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Display
{
    namespace Multiplatform
    {
        /**
         * Sends every frame as a 24 bit image using the kitty graphics protocol, which shows real pixels instead of characters
         * The terminal scales the image to cover the window, so the framebuffer can be smaller than the window without making the image smaller
         * Needs a terminal with kitty graphics support (e.g. kitty, Ghostty, WezTerm, Konsole)
         */
        template<typename T>
        class KittyGraphicsFrameDrawer : public IFrameDrawer
        {
        private:
            std::shared_ptr<FrameBuffer<uint32_t>> framebuffer;
            std::shared_ptr<ITerminalManager<T>> terminal_manager;

            uint32_t max_pixels;
            GraphicsFrameSize frame_size;

            // every frame is a new image that replaces the one before, they alternate between 2 ids so the old one is only removed once the new one is shown
            uint32_t image_id         = 1;
            bool previous_image_shown = false;

            std::string rgb_data;
            std::string image_string;

            void AppendBase64(const std::string& data, size_t begin, size_t end);
            void TranslateFrameBuffer();

        public:
            KittyGraphicsFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager, uint32_t max_pixels = 250000);

            virtual void SetupFrameDrawer() override;
            virtual void ReleaseFrameDrawer() override;

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
