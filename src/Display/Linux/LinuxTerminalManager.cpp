#include "LinuxTerminalManager.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Display
{
    namespace Linux
    {
        static void RestoreTerminalAndExit(int signal)
        {
            // only async-signal-safe calls in here
            const char reset[] = "\033[0m\033[2J\033[H\033[?25h";
            (void)!write(STDOUT_FILENO, reset, sizeof(reset) - 1);
            _exit(128 + signal);
        }

        LinuxTerminalManager::LinuxTerminalManager()
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
            // use the real cell size when the terminal reports its pixel size, many terminals (e.g. Windows Terminal under WSL) report 0
            winsize size{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_xpixel != 0 && size.ws_ypixel != 0 && size.ws_col != 0 && size.ws_row != 0)
                return (static_cast<float>(size.ws_xpixel) / size.ws_col) / (static_cast<float>(size.ws_ypixel) / size.ws_row);

            // most monospace fonts are about twice as tall as they are wide
            return 0.5f;
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
