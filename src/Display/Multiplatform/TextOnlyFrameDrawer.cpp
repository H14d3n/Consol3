#include "TextOnlyFrameDrawer.hpp"

#include "Math/Util/MathUtil.hpp"

#include <algorithm>

#ifdef SYS_WINDOWS
// Windows.h overrides std::min
#define NOMINMAX
#include <Windows.h>
#elif defined(SYS_LINUX)
#include "Display/Windows/WindowsStructsForLinux.hpp"
#endif

namespace Display
{
    namespace Multiplatform
    {
        template<>
        TextOnlyFrameDrawer<CHAR_INFO>::TextOnlyFrameDrawer(std::shared_ptr<FrameBuffer<CHAR_INFO>> framebuffer, std::shared_ptr<ITerminalManager<CHAR_INFO>> terminal_manager) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager)),
            shades({' ', (char)250, ';', '%', (char)176, (char)240, (char)157, (char)177, (char)178, (char)219}),
            // shades(" .:-=+*#%@"),
            shades_count((uint8_t)shades.length())
        {
            this->framebuffer->FillBuffer({{' '}, 0x0F});
        }

        template<>
        TextOnlyFrameDrawer<char>::TextOnlyFrameDrawer(std::shared_ptr<FrameBuffer<char>> framebuffer, std::shared_ptr<ITerminalManager<char>> terminal_manager) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager)),
            // shades({ ' ', (char)250, ';', '%', (char)176, (char)240, (char)157, (char)177, (char)178, (char)219 }),
            shades(" .:-=+*#%@"),
            shades_count((uint8_t)shades.length())
        {
            this->framebuffer->FillBuffer(' ');

            AllocateFrameBufferString();
        }

        template<typename T>
        void TextOnlyFrameDrawer<T>::AllocateFrameBufferString()
        {
            uint64_t width  = framebuffer->GetWidth();
            uint64_t height = framebuffer->GetHeight();

            // every pixel is a single char, plus a cursor position sequence at the start of every row
            framebuffer_string = std::string(width * height + height * MAX_ROW_SEQUENCE_LEN, ' ');
        }

        template<typename T>
        void TextOnlyFrameDrawer<T>::SetupFrameDrawer()
        {
            terminal_manager->SetupTerminalManager();
        }

        template<>
        void TextOnlyFrameDrawer<CHAR_INFO>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            float luminance = color.GetColorNormal();

            uint8_t index = Math::Util::LerpCast<uint8_t>(luminance, 0, shades_count - 1);

            framebuffer->SetValue(x, y, {static_cast<WCHAR>(shades[index]), 0x0F});
        }

        template<>
        void TextOnlyFrameDrawer<char>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            float luminance = color.GetColorNormal();

            uint8_t index = Math::Util::LerpCast<uint8_t>(luminance, 0, shades_count - 1);

            framebuffer->SetValue(x, y, shades[index]);
        }

        template<>
        void TextOnlyFrameDrawer<CHAR_INFO>::DisplayFrame()
        {
            terminal_manager->WriteFrameBufferData(framebuffer->GetFrameBufferData());
        }

        template<>
        void TextOnlyFrameDrawer<char>::DisplayFrame()
        {
            uint64_t current_string_index = 0;
            uint16_t width                = framebuffer->GetWidth();
            const char* data              = framebuffer->GetFrameBufferData();

            for (uint16_t y = 0; y < framebuffer->GetHeight(); y++)
            {
                // position every row explicitly instead of relying on the terminal wrapping at exactly the framebuffer width
                std::string row_string = "\x1b[" + std::to_string(y + 1) + ";1H";
                row_string.copy(framebuffer_string.data() + current_string_index, row_string.length(), 0);
                current_string_index += row_string.length();

                std::copy(data + y * width, data + (y + 1) * width, framebuffer_string.data() + current_string_index);
                current_string_index += width;
            }

            terminal_manager->WriteSizedString(framebuffer_string, current_string_index);
        }

        template<typename T>
        void TextOnlyFrameDrawer<T>::ReportInformation(const std::string& info)
        {
            terminal_manager->SetTitle(info + " | TextOnly");
        }

        template<>
        void TextOnlyFrameDrawer<CHAR_INFO>::ClearFrameBuffer()
        {
            this->framebuffer->FillBuffer({{' '}, 0x0F});
        }

        template<>
        void TextOnlyFrameDrawer<char>::ClearFrameBuffer()
        {
            this->framebuffer->FillBuffer(' ');
        }

        template<typename T>
        const uint16_t TextOnlyFrameDrawer<T>::GetFrameBufferWidth() const
        {
            return framebuffer->GetWidth();
        }

        template<typename T>
        const uint16_t TextOnlyFrameDrawer<T>::GetFrameBufferHeight() const
        {
            return framebuffer->GetHeight();
        }

        template<typename T>
        bool TextOnlyFrameDrawer<T>::UpdateFrameBufferSize()
        {
            uint16_t width;
            uint16_t height;

            if (!terminal_manager->GetDrawableSize(width, height) || (width == framebuffer->GetWidth() && height == framebuffer->GetHeight()))
                return false;

            framebuffer->Resize(width, height);
            ClearFrameBuffer();
            AllocateFrameBufferString();

            return true;
        }

        template<typename T>
        float TextOnlyFrameDrawer<T>::GetPixelAspectRatio() const
        {
            return terminal_manager->GetCellAspectRatio();
        }
    }
}
