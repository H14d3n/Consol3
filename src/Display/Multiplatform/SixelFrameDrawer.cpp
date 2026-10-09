#include "SixelFrameDrawer.hpp"

#include <algorithm>
#include <charconv>

namespace Display
{
    namespace Multiplatform
    {
        // every color register a terminal can have, palettes are never bigger than this
        static constexpr uint16_t MAX_PALETTE_SIZE = 256;

        template<typename T>
        SixelFrameDrawer<T>::SixelFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager, uint16_t palette_size, uint32_t max_pixels) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager)),
            palette_size(std::clamp<uint16_t>(palette_size, 2, MAX_PALETTE_SIZE)),
            max_pixels(max_pixels)
        {
            if (!GetGraphicsFrameSize(*this->terminal_manager, max_pixels, frame_size))
            {
                frame_size.framebuffer_width  = this->framebuffer->GetWidth();
                frame_size.framebuffer_height = this->framebuffer->GetHeight();
            }

            ResizeBuffers();
            ClearFrameBuffer();
        }

        template<typename T>
        void SixelFrameDrawer<T>::ResizeBuffers()
        {
            framebuffer->Resize(frame_size.framebuffer_width, frame_size.framebuffer_height);
            indexed_framebuffer.assign(static_cast<size_t>(frame_size.framebuffer_width) * frame_size.framebuffer_height, 0);

            size_t image_width = static_cast<size_t>(frame_size.framebuffer_width) * frame_size.scale;
            band_bits.assign(MAX_PALETTE_SIZE * image_width, 0);
            band_color_used.assign(MAX_PALETTE_SIZE, false);
            band_color_begin.assign(MAX_PALETTE_SIZE, 0);
            band_color_end.assign(MAX_PALETTE_SIZE, 0);
            band_colors.clear();
        }

        template<typename T>
        void SixelFrameDrawer<T>::AppendNumber(uint32_t number)
        {
            char digits[10];
            auto result = std::to_chars(digits, digits + sizeof(digits), number);
            sixel_string.append(digits, result.ptr);
        }

        template<typename T>
        void SixelFrameDrawer<T>::AppendSixelRun(char sixel, uint16_t count)
        {
            // a repeat introducer is only shorter from 4 sixels on
            if (count > 3)
            {
                sixel_string += '!';
                AppendNumber(count);
                sixel_string += sixel;
            }
            else
            {
                sixel_string.append(count, sixel);
            }
        }

        template<typename T>
        void SixelFrameDrawer<T>::EncodeBand(uint16_t band_y)
        {
            uint16_t scale        = frame_size.scale;
            uint16_t fb_width     = frame_size.framebuffer_width;
            uint32_t image_width  = static_cast<uint32_t>(fb_width) * scale;
            uint32_t image_height = static_cast<uint32_t>(frame_size.framebuffer_height) * scale;

            // a sixel is a column of 6 pixels, collect which of the 6 rows every color covers in every column
            for (uint8_t sixel_bit = 0; sixel_bit < 6; sixel_bit++)
            {
                uint32_t image_y = band_y + sixel_bit;
                if (image_y >= image_height)
                    break;

                const uint8_t* row = indexed_framebuffer.data() + static_cast<size_t>(image_y / scale) * fb_width;
                uint8_t bit        = 1 << sixel_bit;

                for (uint16_t fb_x = 0; fb_x < fb_width; fb_x++)
                {
                    uint8_t color   = row[fb_x];
                    uint16_t x      = fb_x * scale;
                    uint8_t* sixels = band_bits.data() + color * image_width + x;

                    for (uint16_t i = 0; i < scale; i++)
                        sixels[i] |= bit;

                    if (!band_color_used[color])
                    {
                        band_color_used[color]  = true;
                        band_color_begin[color] = x;
                        band_colors.push_back(color);
                    }
                    band_color_begin[color] = std::min(band_color_begin[color], x);
                    band_color_end[color]   = std::max<uint16_t>(band_color_end[color], x + scale);
                }
            }

            // every color is drawn over the band on its own, $ goes back to the start of the band for the next one
            for (size_t i = 0; i < band_colors.size(); i++)
            {
                uint8_t color = band_colors[i];

                if (i != 0)
                    sixel_string += '$';
                sixel_string += '#';
                AppendNumber(color);

                uint16_t begin  = band_color_begin[color];
                uint16_t end    = band_color_end[color];
                uint8_t* sixels = band_bits.data() + color * image_width;

                if (begin > 0)
                    AppendSixelRun('?', begin);

                uint16_t x = begin;
                while (x < end)
                {
                    uint8_t bits   = sixels[x];
                    uint16_t count = 1;
                    while (x + count < end && sixels[x + count] == bits)
                        count++;

                    AppendSixelRun(static_cast<char>('?' + bits), count);
                    x += count;
                }

                std::fill(sixels + begin, sixels + end, 0);
                band_color_used[color] = false;
                band_color_end[color]  = 0;
            }

            band_colors.clear();
        }

        template<typename T>
        void SixelFrameDrawer<T>::TranslateFrameBuffer()
        {
            quantizer.BuildPalette(*framebuffer, palette_size);

            const uint32_t* pixels = framebuffer->GetFrameBufferData();
            for (size_t i = 0; i < indexed_framebuffer.size(); i++)
                indexed_framebuffer[i] = quantizer.GetPaletteIndex(pixels[i]);

            uint32_t image_width  = static_cast<uint32_t>(frame_size.framebuffer_width) * frame_size.scale;
            uint32_t image_height = static_cast<uint32_t>(frame_size.framebuffer_height) * frame_size.scale;

            sixel_string.clear();

            // the raster attributes set square pixels and the image size, so the terminal doesn't have to guess it
            sixel_string += "\033P0;1;0q\"1;1;";
            AppendNumber(image_width);
            sixel_string += ';';
            AppendNumber(image_height);

            // colors are given in percent
            const std::vector<RGBColor>& palette = quantizer.GetPalette();
            for (size_t i = 0; i < palette.size(); i++)
            {
                sixel_string += '#';
                AppendNumber(static_cast<uint32_t>(i));
                sixel_string += ";2;";
                AppendNumber((palette[i].r * 100 + 127) / 255);
                sixel_string += ';';
                AppendNumber((palette[i].g * 100 + 127) / 255);
                sixel_string += ';';
                AppendNumber((palette[i].b * 100 + 127) / 255);
            }

            for (uint32_t band_y = 0; band_y < image_height; band_y += 6)
            {
                // - moves down to the next band, not after the last one, that could make the terminal scroll
                if (band_y != 0)
                    sixel_string += '-';

                EncodeBand(static_cast<uint16_t>(band_y));
            }

            sixel_string += "\033\\";
        }

        template<typename T>
        void SixelFrameDrawer<T>::SetupFrameDrawer()
        {
            terminal_manager->SetupTerminalManager();
        }

        template<typename T>
        void SixelFrameDrawer<T>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            framebuffer->SetValue(x, y, color.GetHexValues());
        }

        template<typename T>
        void SixelFrameDrawer<T>::DisplayFrame()
        {
            TranslateFrameBuffer();
            terminal_manager->WriteSizedString(sixel_string, sixel_string.size());
        }

        template<typename T>
        void SixelFrameDrawer<T>::ClearFrameBuffer()
        {
            framebuffer->FillBuffer(0x000000);
        }

        template<typename T>
        void SixelFrameDrawer<T>::ReportInformation(const std::string& info)
        {
            terminal_manager->SetTitle(info + " | Sixel");
        }

        template<typename T>
        const uint16_t SixelFrameDrawer<T>::GetFrameBufferWidth() const
        {
            return framebuffer->GetWidth();
        }

        template<typename T>
        const uint16_t SixelFrameDrawer<T>::GetFrameBufferHeight() const
        {
            return framebuffer->GetHeight();
        }

        template<typename T>
        bool SixelFrameDrawer<T>::UpdateFrameBufferSize()
        {
            GraphicsFrameSize new_size;
            if (!GetGraphicsFrameSize(*terminal_manager, max_pixels, new_size) || new_size == frame_size)
                return false;

            frame_size = new_size;
            ResizeBuffers();
            ClearFrameBuffer();

            return true;
        }

        template<typename T>
        float SixelFrameDrawer<T>::GetPixelAspectRatio() const
        {
            // sixel pixels are the terminal's own pixels, which are square
            return 1.0f;
        }

        template class SixelFrameDrawer<char>;
    }
}
