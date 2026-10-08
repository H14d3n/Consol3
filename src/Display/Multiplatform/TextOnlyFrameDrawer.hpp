#ifndef TEXTONLYFRAMEDRAWER_HPP
#define TEXTONLYFRAMEDRAWER_HPP

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
        template<typename T>
        class TextOnlyFrameDrawer : public IFrameDrawer
        {
        private:
            std::shared_ptr<FrameBuffer<T>> framebuffer;
            std::shared_ptr<ITerminalManager<T>> terminal_manager;

            const std::string shades;
            const uint8_t shades_count;

            // only used when the frame is written as a string (T = char)
            std::string framebuffer_string;

            // longest possible row position sequence: \x1b[65535;1H
            static constexpr uint64_t MAX_ROW_SEQUENCE_LEN = 11;

            void AllocateFrameBufferString();

        public:
            TextOnlyFrameDrawer(std::shared_ptr<FrameBuffer<T>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager);

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
