#ifndef GRAPHICSFRAMESIZE_HPP
#define GRAPHICSFRAMESIZE_HPP

#include "Display/ITerminalManager.hpp"

#include <algorithm>
#include <cstdint>

namespace Display
{
    namespace Multiplatform
    {
        /**
         * How the framebuffer of a frame drawer that sends real images to the terminal maps onto the terminal
         */
        struct GraphicsFrameSize
        {
            // the cells covered by the image
            uint16_t columns = 0;
            uint16_t rows    = 0;

            // pixels of a single cell, estimated when the terminal doesn't report it
            uint16_t cell_width  = 0;
            uint16_t cell_height = 0;
            bool cell_size_known = false;

            uint16_t framebuffer_width  = 0;
            uint16_t framebuffer_height = 0;

            // terminal pixels per framebuffer pixel, in each direction
            uint16_t scale = 1;

            [[nodiscard]] bool operator==(const GraphicsFrameSize& other) const
            {
                return columns == other.columns && rows == other.rows && cell_width == other.cell_width && cell_height == other.cell_height && cell_size_known == other.cell_size_known
                       && framebuffer_width == other.framebuffer_width && framebuffer_height == other.framebuffer_height && scale == other.scale;
            }
        };

        // the size of a cell in a VT340, terminals that emulate its sixel graphics (like Windows Terminal) use it no matter the font size
        static constexpr uint16_t DEFAULT_CELL_PIXEL_WIDTH  = 10;
        static constexpr uint16_t DEFAULT_CELL_PIXEL_HEIGHT = 20;

        /**
         * Covers the whole drawable area of the terminal at its pixel resolution, rendering every pixel costs too much on big or high dpi displays,
         * so the framebuffer is scaled down by a whole number until it has at most max_pixels
         */
        template<typename T>
        bool GetGraphicsFrameSize(const ITerminalManager<T>& terminal_manager, uint32_t max_pixels, GraphicsFrameSize& size)
        {
            if (!terminal_manager.GetDrawableSize(size.columns, size.rows) || size.columns == 0 || size.rows == 0)
                return false;

            size.cell_size_known = terminal_manager.GetCellPixelSize(size.cell_width, size.cell_height) && size.cell_width != 0 && size.cell_height != 0;
            if (!size.cell_size_known)
            {
                size.cell_width  = DEFAULT_CELL_PIXEL_WIDTH;
                size.cell_height = DEFAULT_CELL_PIXEL_HEIGHT;
            }

            uint32_t area_width  = static_cast<uint32_t>(size.columns) * size.cell_width;
            uint32_t area_height = static_cast<uint32_t>(size.rows) * size.cell_height;

            size.scale = 1;
            while ((area_width / size.scale) * (area_height / size.scale) > max_pixels)
                size.scale++;

            size.framebuffer_width  = static_cast<uint16_t>(std::min<uint32_t>(area_width / size.scale, UINT16_MAX));
            size.framebuffer_height = static_cast<uint16_t>(std::min<uint32_t>(area_height / size.scale, UINT16_MAX));

            return size.framebuffer_width > 0 && size.framebuffer_height > 0;
        }
    }
}

#endif
