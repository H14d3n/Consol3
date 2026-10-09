#include "KittyGraphicsFrameDrawer.hpp"

#include <algorithm>

namespace Display
{
    namespace Multiplatform
    {
        // the protocol allows at most 4096 base64 characters per escape sequence, which is 3072 bytes of data
        static constexpr size_t MAX_CHUNK_BYTES = 3072;

        static constexpr char BASE64_CHARACTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        template<typename T>
        KittyGraphicsFrameDrawer<T>::KittyGraphicsFrameDrawer(std::shared_ptr<FrameBuffer<uint32_t>> framebuffer, std::shared_ptr<ITerminalManager<T>> terminal_manager, uint32_t max_pixels) :
            framebuffer(std::move(framebuffer)),
            terminal_manager(std::move(terminal_manager)),
            max_pixels(max_pixels)
        {
            if (GetGraphicsFrameSize(*this->terminal_manager, max_pixels, frame_size))
            {
                this->framebuffer->Resize(frame_size.framebuffer_width, frame_size.framebuffer_height);
            }
            else
            {
                frame_size.framebuffer_width  = this->framebuffer->GetWidth();
                frame_size.framebuffer_height = this->framebuffer->GetHeight();
            }

            ClearFrameBuffer();
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::AppendBase64(const std::string& data, size_t begin, size_t end)
        {
            size_t i = begin;
            for (; i + 2 < end; i += 3)
            {
                uint32_t triple = static_cast<uint8_t>(data[i]) << 16 | static_cast<uint8_t>(data[i + 1]) << 8 | static_cast<uint8_t>(data[i + 2]);
                image_string += BASE64_CHARACTERS[triple >> 18 & 0x3F];
                image_string += BASE64_CHARACTERS[triple >> 12 & 0x3F];
                image_string += BASE64_CHARACTERS[triple >> 6 & 0x3F];
                image_string += BASE64_CHARACTERS[triple & 0x3F];
            }

            if (i < end)
            {
                uint32_t triple = static_cast<uint8_t>(data[i]) << 16;
                if (i + 1 < end)
                    triple |= static_cast<uint8_t>(data[i + 1]) << 8;

                image_string += BASE64_CHARACTERS[triple >> 18 & 0x3F];
                image_string += BASE64_CHARACTERS[triple >> 12 & 0x3F];
                image_string += i + 1 < end ? BASE64_CHARACTERS[triple >> 6 & 0x3F] : '=';
                image_string += '=';
            }
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::TranslateFrameBuffer()
        {
            const uint32_t* pixels = framebuffer->GetFrameBufferData();
            size_t pixel_count     = static_cast<size_t>(framebuffer->GetWidth()) * framebuffer->GetHeight();

            rgb_data.resize(pixel_count * 3);
            for (size_t i = 0; i < pixel_count; i++)
            {
                rgb_data[i * 3]     = static_cast<char>(pixels[i] >> 16);
                rgb_data[i * 3 + 1] = static_cast<char>(pixels[i] >> 8);
                rgb_data[i * 3 + 2] = static_cast<char>(pixels[i]);
            }

            uint32_t previous_image_id = image_id == 1 ? 2 : 1;

            image_string.clear();

            for (size_t chunk_begin = 0; chunk_begin < rgb_data.size(); chunk_begin += MAX_CHUNK_BYTES)
            {
                size_t chunk_end = std::min(chunk_begin + MAX_CHUNK_BYTES, rgb_data.size());

                image_string += "\033_G";

                // the first chunk describes the image: raw RGB, scaled over the drawable cells, without moving the cursor and without answers from the terminal
                if (chunk_begin == 0)
                {
                    image_string += "a=T,f=24,s=" + std::to_string(framebuffer->GetWidth()) + ",v=" + std::to_string(framebuffer->GetHeight()) + ",i=" + std::to_string(image_id)
                                  + ",p=1,c=" + std::to_string(frame_size.columns) + ",r=" + std::to_string(frame_size.rows) + ",C=1,q=2,";
                }

                image_string += chunk_end < rgb_data.size() ? "m=1;" : "m=0;";
                AppendBase64(rgb_data, chunk_begin, chunk_end);
                image_string += "\033\\";
            }

            if (previous_image_shown)
                image_string += "\033_Ga=d,d=I,i=" + std::to_string(previous_image_id) + ",q=2\033\\";

            previous_image_shown = true;
            image_id             = previous_image_id;
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::SetupFrameDrawer()
        {
            terminal_manager->SetupTerminalManager();
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::ReleaseFrameDrawer()
        {
            // images stay over the text until they're deleted
            std::string delete_images = "\033_Ga=d,d=I,i=1,q=2\033\\\033_Ga=d,d=I,i=2,q=2\033\\";
            terminal_manager->WriteSizedString(delete_images, delete_images.size());

            previous_image_shown = false;
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::SetPixel(uint16_t x, uint16_t y, RGBColor color)
        {
            framebuffer->SetValue(x, y, color.GetHexValues());
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::DisplayFrame()
        {
            TranslateFrameBuffer();
            terminal_manager->WriteSizedString(image_string, image_string.size());
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::ClearFrameBuffer()
        {
            framebuffer->FillBuffer(0x000000);
        }

        template<typename T>
        void KittyGraphicsFrameDrawer<T>::ReportInformation(const std::string& info)
        {
            terminal_manager->SetTitle(info + " | KittyGraphics");
        }

        template<typename T>
        const uint16_t KittyGraphicsFrameDrawer<T>::GetFrameBufferWidth() const
        {
            return framebuffer->GetWidth();
        }

        template<typename T>
        const uint16_t KittyGraphicsFrameDrawer<T>::GetFrameBufferHeight() const
        {
            return framebuffer->GetHeight();
        }

        template<typename T>
        bool KittyGraphicsFrameDrawer<T>::UpdateFrameBufferSize()
        {
            GraphicsFrameSize new_size;
            if (!GetGraphicsFrameSize(*terminal_manager, max_pixels, new_size) || new_size == frame_size)
                return false;

            frame_size = new_size;
            framebuffer->Resize(frame_size.framebuffer_width, frame_size.framebuffer_height);
            ClearFrameBuffer();

            return true;
        }

        template<typename T>
        float KittyGraphicsFrameDrawer<T>::GetPixelAspectRatio() const
        {
            // the image is stretched over the cells, if the cell size was only estimated the pixels aren't exactly square
            float pixel_width  = terminal_manager->GetCellAspectRatio() * frame_size.columns / framebuffer->GetWidth();
            float pixel_height = static_cast<float>(frame_size.rows) / framebuffer->GetHeight();

            return pixel_width / pixel_height;
        }

        template class KittyGraphicsFrameDrawer<char>;
    }
}
