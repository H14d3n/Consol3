#include "VT24BitHalfBlockFrameDrawer.hpp"

#include <algorithm>

namespace Display
{
    namespace Multiplatform
    {
        template<typename T>
        VT24BitHalfBlockFrameDrawer<T>::VT24BitHalfBlockFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager))
        {
            this->framebuffer->FillBuffer(0x000000);

            AllocateFrameBufferString();
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::AllocateFrameBufferString()
        {
            uint64_t width = framebuffer->GetWidth();
            uint64_t rows  = (framebuffer->GetHeight() + 1) / 2;

            // worst case: both colors change on every cell, plus a cursor position sequence at the start of every row
            framebuffer_string     = std::string(width * rows * MAX_CELL_LEN + rows * MAX_ROW_SEQUENCE_LEN, ' ');
            framebuffer_string_len = 0;
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::WriteColor(uint64_t& string_index, const char* prefix, uint32_t color)
        {
            RGBColor rgb_color = RGBColor(color);

            std::string color_string = prefix + std::to_string(rgb_color.r) + ";" + std::to_string(rgb_color.g) + ";" + std::to_string(rgb_color.b);
            color_string.copy(framebuffer_string.data() + string_index, color_string.length(), 0);
            string_index += color_string.length();
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::SetupFrameDrawer()
        {
            terminal_manager->SetupTerminalManager();
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            framebuffer->SetValue(x, y, color.GetHexValues());
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::TranslateFrameBuffer()
        {
            uint64_t current_string_index = 0;

            // the colors are only sent when they change, the first cell always sets them
            uint32_t last_foreground = 0x000000;
            uint32_t last_background = 0x000000;
            bool foreground_set      = false;
            bool background_set      = false;

            uint16_t rows = (framebuffer->GetHeight() + 1) / 2;

            for (uint16_t row = 0; row < rows; row++)
            {
                // position every row explicitly instead of using newlines, so the output can't be broken by how the terminal wraps lines
                std::string row_string = "\x1b[" + std::to_string(row + 1) + ";1H";
                row_string.copy(framebuffer_string.data() + current_string_index, row_string.length(), 0);
                current_string_index += row_string.length();

                uint16_t top_y    = row * 2;
                uint16_t bottom_y = std::min<uint16_t>(top_y + 1, framebuffer->GetHeight() - 1);

                for (uint16_t x = 0; x < framebuffer->GetWidth(); x++)
                {
                    uint32_t top    = framebuffer->GetValue(x, top_y);
                    uint32_t bottom = framebuffer->GetValue(x, bottom_y);

                    // both halves with the same color only need the background, which saves a lot in flat areas
                    bool solid          = top == bottom;
                    bool set_foreground = !solid && (!foreground_set || last_foreground != top);
                    bool set_background = !background_set || last_background != bottom;

                    if (set_foreground || set_background)
                    {
                        framebuffer_string.data()[current_string_index++] = '\x1b';
                        framebuffer_string.data()[current_string_index++] = '[';

                        if (set_foreground)
                        {
                            WriteColor(current_string_index, "38;2;", top);
                            last_foreground = top;
                            foreground_set  = true;
                        }

                        if (set_background)
                        {
                            if (set_foreground)
                                framebuffer_string.data()[current_string_index++] = ';';

                            WriteColor(current_string_index, "48;2;", bottom);
                            last_background = bottom;
                            background_set  = true;
                        }

                        framebuffer_string.data()[current_string_index++] = 'm';
                    }

                    if (solid)
                    {
                        framebuffer_string.data()[current_string_index++] = ' ';
                    }
                    else
                    {
                        upper_half_block.copy(framebuffer_string.data() + current_string_index, upper_half_block.length(), 0);
                        current_string_index += upper_half_block.length();
                    }
                }
            }

            framebuffer_string_len = current_string_index;
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::DisplayFrame()
        {
            TranslateFrameBuffer();
            terminal_manager->WriteSizedString(framebuffer_string, framebuffer_string_len);
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::ReportInformation(const std::string& info)
        {
            terminal_manager->SetTitle(info + " | VT24BitHalfBlock");
        }

        template<typename T>
        void VT24BitHalfBlockFrameDrawer<T>::ClearFrameBuffer()
        {
            this->framebuffer->FillBuffer(0x000000);
        }

        template<typename T>
        const uint16_t VT24BitHalfBlockFrameDrawer<T>::GetFrameBufferWidth() const
        {
            return framebuffer->GetWidth();
        }

        template<typename T>
        const uint16_t VT24BitHalfBlockFrameDrawer<T>::GetFrameBufferHeight() const
        {
            return framebuffer->GetHeight();
        }

        template<typename T>
        bool VT24BitHalfBlockFrameDrawer<T>::UpdateFrameBufferSize()
        {
            uint16_t width;
            uint16_t rows;

            if (!terminal_manager->GetDrawableSize(width, rows) || (width == framebuffer->GetWidth() && rows * 2 == framebuffer->GetHeight()))
                return false;

            framebuffer->Resize(width, rows * 2);
            ClearFrameBuffer();
            AllocateFrameBufferString();

            return true;
        }

        template<typename T>
        float VT24BitHalfBlockFrameDrawer<T>::GetPixelAspectRatio() const
        {
            // every cell is split in 2 pixels vertically
            return terminal_manager->GetCellAspectRatio() * 2.0f;
        }

        template class VT24BitHalfBlockFrameDrawer<char>;
    }
}
