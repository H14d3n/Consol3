#include "VT8BitFrameDrawer.hpp"

#include "Display/ColorMapping.hpp"

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
        VT8BitFrameDrawer<char>::VT8BitFrameDrawer(std::shared_ptr<FrameBuffer<uint8_t>> framebuffer, std::shared_ptr<ITerminalManager<char>> terminal_manager) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager))
        {
            this->framebuffer->FillBuffer(0x000000);

            AllocateFrameBufferString();
        }

        template<>
        VT8BitFrameDrawer<CHAR_INFO>::VT8BitFrameDrawer(std::shared_ptr<FrameBuffer<uint8_t>> framebuffer, std::shared_ptr<ITerminalManager<CHAR_INFO>> terminal_manager) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager))
        {
            this->framebuffer->FillBuffer(0x000000);

            AllocateFrameBufferString();
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::AllocateFrameBufferString()
        {
            uint64_t width  = framebuffer->GetWidth();
            uint64_t height = framebuffer->GetHeight();

            // worst case: a color escape sequence before every pixel, plus a cursor position sequence at the start of every row
            framebuffer_string     = std::string(width * height * (MAX_COLOR_SEQUENCE_LEN + 1) + height * MAX_ROW_SEQUENCE_LEN, ' ');
            framebuffer_string_len = 0;
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::SetupFrameDrawer()
        {
            terminal_manager->SetupTerminalManager();
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            uint32_t real_color_hex = color.GetHexValues();

            framebuffer->SetValue(x, y, indexed_colors[vt8bit_color_mapping[real_color_hex]].console_color_index);
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::TranslateFrameBuffer()
        {
            uint8_t last_color            = 0x000000;
            uint64_t current_string_index = 0;

            for (uint16_t y = 0; y < framebuffer->GetHeight(); y++)
            {
                // position every row explicitly instead of using newlines, so the output can't be broken by how the terminal wraps lines
                std::string row_string = "\x1b[" + std::to_string(y + 1) + ";1H";
                row_string.copy(framebuffer_string.data() + current_string_index, row_string.length(), 0);
                current_string_index += row_string.length();

                for (uint16_t x = 0; x < framebuffer->GetWidth(); x++)
                {
                    uint8_t current_color = framebuffer->GetValue(x, y);

                    // always set color on the first pixel
                    if (current_color != last_color || (y == 0 && x == 0))
                    {
                        std::string index_str = std::to_string(current_color);
                        uint8_t index_str_len = (uint8_t)index_str.length();

                        esc_sequence_start.copy(framebuffer_string.data() + current_string_index, esc_sequence_start_len, 0);
                        current_string_index += esc_sequence_start_len;

                        index_str.copy(framebuffer_string.data() + current_string_index, index_str_len, 0);
                        current_string_index += index_str_len;

                        esc_sequence_end.copy(framebuffer_string.data() + current_string_index, esc_sequence_len, 0);
                        current_string_index += esc_sequence_len;

                        last_color = current_color;
                    }

                    framebuffer_string.data()[current_string_index++] = ' ';
                }
            }

            framebuffer_string_len = current_string_index;
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::DisplayFrame()
        {
            TranslateFrameBuffer();
            terminal_manager->WriteSizedString(framebuffer_string, framebuffer_string_len);
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::ReportInformation(const std::string& info)
        {
            terminal_manager->SetTitle(info + " | VT8Bit");
        }

        template<typename T>
        void VT8BitFrameDrawer<T>::ClearFrameBuffer()
        {
            this->framebuffer->FillBuffer(0x000000);
        }

        template<typename T>
        const uint16_t VT8BitFrameDrawer<T>::GetFrameBufferWidth() const
        {
            return framebuffer->GetWidth();
        }

        template<typename T>
        const uint16_t VT8BitFrameDrawer<T>::GetFrameBufferHeight() const
        {
            return framebuffer->GetHeight();
        }

        template<typename T>
        bool VT8BitFrameDrawer<T>::UpdateFrameBufferSize()
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
        float VT8BitFrameDrawer<T>::GetPixelAspectRatio() const
        {
            return terminal_manager->GetCellAspectRatio();
        }
    }
}