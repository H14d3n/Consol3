#include "LinuxTerminalManager.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace Display
{
    namespace Linux
    {
        // how long to wait for the terminal to answer the queries, terminals usually answer within a few ms
        static constexpr int GRAPHICS_QUERY_TIMEOUT_MS = 500;

        static void RestoreTerminalAndExit(int signal)
        {
            // only async-signal-safe calls in here
            const char reset[] = "\033[0m\033[2J\033[H\033[?25h";
            (void)!write(STDOUT_FILENO, reset, sizeof(reset) - 1);
            _exit(128 + signal);
        }

        static std::vector<int> SplitParameters(const std::string& parameters)
        {
            std::vector<int> values;
            int value      = 0;
            bool has_value = false;

            for (char c : parameters)
            {
                if (c >= '0' && c <= '9')
                {
                    value     = value * 10 + (c - '0');
                    has_value = true;
                }
                else if (c == ';')
                {
                    values.push_back(has_value ? value : 0);
                    value     = 0;
                    has_value = false;
                }
            }
            values.push_back(has_value ? value : 0);

            return values;
        }

        /**
         * Reads the answers to the graphics queries out of everything the terminal sent, anything else (like a mouse event from a click) is skipped
         * Returns true once the device attributes answer arrived, it's always the last one
         */
        static bool ParseGraphicsQueryAnswers(const std::string& input, TerminalGraphicsSupport& support)
        {
            size_t i = 0;
            while (i < input.size())
            {
                if (input[i] != '\033' || i + 1 >= input.size())
                {
                    i++;
                    continue;
                }

                // APC, the kitty graphics answer: ESC _ G <keys> ; <message> ESC \.
                if (input[i + 1] == '_')
                {
                    size_t end = input.find("\033\\", i + 2);
                    if (end == std::string::npos)
                        return false;

                    std::string body = input.substr(i + 2, end - i - 2);
                    if (body.rfind("Gi=31", 0) == 0 && body.find(";OK") != std::string::npos)
                        support.kitty_graphics = true;

                    i = end + 2;
                    continue;
                }

                if (input[i + 1] != '[')
                {
                    i++;
                    continue;
                }

                // CSI: parameters up to the final byte
                size_t final_index = i + 2;
                while (final_index < input.size() && (input[final_index] < 0x40 || input[final_index] > 0x7E))
                    final_index++;
                if (final_index >= input.size())
                    return false;

                std::string parameters = input.substr(i + 2, final_index - i - 2);
                char final             = input[final_index];
                i                      = final_index + 1;

                // primary device attributes: ESC [ ? 62 ; 4 ; ... c, 4 means sixel
                if (final == 'c' && !parameters.empty() && parameters[0] == '?')
                {
                    std::vector<int> attributes = SplitParameters(parameters.substr(1));
                    for (size_t attribute = 1; attribute < attributes.size(); attribute++)
                    {
                        if (attributes[attribute] == 4)
                            support.sixel = true;
                    }
                    return true;
                }

                // XTSMGRAPHICS number of color registers: ESC [ ? 1 ; 0 ; <registers> S
                if (final == 'S' && !parameters.empty() && parameters[0] == '?')
                {
                    std::vector<int> values = SplitParameters(parameters.substr(1));
                    if (values.size() >= 3 && values[0] == 1 && values[1] == 0)
                        support.sixel_color_registers = static_cast<uint16_t>(std::min(values[2], 65535));
                }

                // cell size in pixels: ESC [ 6 ; <height> ; <width> t
                if (final == 't')
                {
                    std::vector<int> values = SplitParameters(parameters);
                    if (values.size() >= 3 && values[0] == 6 && values[1] > 0 && values[2] > 0)
                    {
                        support.cell_pixel_height = static_cast<uint16_t>(values[1]);
                        support.cell_pixel_width  = static_cast<uint16_t>(values[2]);
                    }
                }
            }

            return false;
        }

        TerminalGraphicsSupport LinuxTerminalManager::QueryGraphicsSupport()
        {
            TerminalGraphicsSupport support;

            termios original_termios;
            if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &original_termios) != 0)
                return support;

            // the answers come in through the input, read them as they come without echoing them
            termios raw = original_termios;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
                return support;

            // kitty graphics protocol, number of sixel color registers, cell size in pixels and finally the device attributes
            // every terminal answers the device attributes and terminals answer in order, so once that answer is in, every other answer is too
            std::cout << "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"
                      << "\033[?1;1;0S"
                      << "\033[16t"
                      << "\033[c" << std::flush;

            std::string input;
            bool answered = false;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(GRAPHICS_QUERY_TIMEOUT_MS);

            while (!answered)
            {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if (remaining <= 0)
                    break;

                pollfd stdin_poll = {STDIN_FILENO, POLLIN, 0};
                if (poll(&stdin_poll, 1, static_cast<int>(remaining)) <= 0)
                    continue;

                char buffer[256];
                ssize_t read_count = read(STDIN_FILENO, buffer, sizeof(buffer));
                if (read_count <= 0)
                    continue;

                input.append(buffer, read_count);

                TerminalGraphicsSupport parsed;
                answered = ParseGraphicsQueryAnswers(input, parsed);
                if (answered)
                    support = parsed;
            }

            tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);

            return support;
        }

        LinuxTerminalManager::LinuxTerminalManager() : graphics_support(QueryGraphicsSupport())
        {
            // the cursor is hidden and colors are changed while running, make sure Ctrl+C doesn't leave the terminal like that
            std::signal(SIGINT, RestoreTerminalAndExit);
            std::signal(SIGTERM, RestoreTerminalAndExit);
        }

        LinuxTerminalManager::~LinuxTerminalManager()
        {
            // restore the console to its original state
            EnableCursor();
        }

        bool LinuxTerminalManager::QueryTerminalSize(uint16_t& width, uint16_t& height)
        {
            winsize size{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_col == 0 || size.ws_row < 2)
                return false;

            width = size.ws_col;
            // the last row is kept free so the cursor never reaches the bottom of the screen, which would make the terminal scroll
            height = size.ws_row - 1;

            return true;
        }

        const TerminalGraphicsSupport& LinuxTerminalManager::GetGraphicsSupport() const
        {
            return graphics_support;
        }

        void LinuxTerminalManager::SetupTerminalManager()
        {
            // reset colors before clearing, otherwise the screen is cleared with the last used background color
            std::cout << "\033[0m\033[2J";

            DisableCursor();
        }

        void LinuxTerminalManager::SetPalette(const uint32_t palette[])
        {
        }

        void LinuxTerminalManager::SetTitle(const std::string& title)
        {
            std::cout << "\033]2;" << title << "\007";
        }

        void LinuxTerminalManager::EnableCursor()
        {
            std::cout << "\033[0m\033[?25h" << std::flush;
        }

        void LinuxTerminalManager::DisableCursor()
        {
            std::cout << "\033[?25l" << std::flush;
        }

        bool LinuxTerminalManager::GetDrawableSize(uint16_t& width, uint16_t& height) const
        {
            return QueryTerminalSize(width, height);
        }

        float LinuxTerminalManager::GetCellAspectRatio() const
        {
            uint16_t cell_width;
            uint16_t cell_height;
            if (GetCellPixelSize(cell_width, cell_height))
                return static_cast<float>(cell_width) / cell_height;

            // most monospace fonts are about twice as tall as they are wide
            return 0.5f;
        }

        bool LinuxTerminalManager::GetCellPixelSize(uint16_t& width, uint16_t& height) const
        {
            // the pixel size from the kernel follows font changes, but many terminals (e.g. Windows Terminal under WSL) report 0
            winsize size{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_xpixel != 0 && size.ws_ypixel != 0 && size.ws_col != 0 && size.ws_row != 0)
            {
                width  = size.ws_xpixel / size.ws_col;
                height = size.ws_ypixel / size.ws_row;
                return true;
            }

            // otherwise use what the terminal answered when asked at startup
            if (graphics_support.cell_pixel_width != 0 && graphics_support.cell_pixel_height != 0)
            {
                width  = graphics_support.cell_pixel_width;
                height = graphics_support.cell_pixel_height;
                return true;
            }

            return false;
        }

        void LinuxTerminalManager::WriteFrameBufferData(const char* data)
        {
            // clear screen
            std::cout << "\033[0;0H";

            printf("%s", data);
        }

        void LinuxTerminalManager::WriteSizedString(const std::string& string, uint64_t size)
        {
            // move to the top left, the string positions every row itself so it doesn't rely on line wrapping
            fputs("\033[H", stdout);
            fwrite(string.data(), 1, size, stdout);
            // write the whole frame at once, otherwise it shows up in pieces whenever the stdout buffer fills
            fflush(stdout);
        }
    }

}
