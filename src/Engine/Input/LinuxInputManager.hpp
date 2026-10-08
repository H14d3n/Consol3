#ifdef SYS_LINUX
#ifndef LINUXINPUTMANAGER_HPP
#define LINUXINPUTMANAGER_HPP

#include "IInputManager.hpp"

#include <array>
#include <cstdint>
#include <linux/input.h>
#include <optional>
#include <string>
#include <termios.h>
#include <vector>

namespace Engine
{
    namespace Input
    {
        /**
         * Reads input from two sources:
         *  - the terminal (stdin), works everywhere: WSL, SSH, containers, any terminal emulator
         *    the terminal is asked for the kitty keyboard protocol and win32-input-mode, which report key releases,
         *    terminals that support neither only report key presses, so held keys are estimated from the key repeat
         *  - the keyboard devices in /dev/input, only available on a local machine with read access (root or the input group),
         *    when available these are used for the keyboard as they always report the real key state
         */
        class LinuxInputManager : public IInputManager
        {
        private:
            struct TerminalKeyState
            {
                // set by terminals that report releases
                bool down = false;
                // set by terminals that only report presses, the key counts as held until this time (ms)
                int64_t held_until = 0;
                // flipped on every new press
                bool toggled = false;
                // a press and release read together (a quick tap during a slow frame) still count as held for one update
                bool pressed_this_update    = false;
                bool release_on_next_update = false;
            };

            std::vector<int> keyboard_device_files;
            uint8_t keyboard_state[KEY_MAX / 8 + 1];
            uint8_t keyboard_leds[LED_MAX / 8 + 1];

            bool terminal_input = false;
            termios original_termios;
            std::string pending_input;
            int64_t pending_input_time = 0;

            std::array<TerminalKeyState, static_cast<size_t>(Key::KEY_COUNT)> terminal_keys;
            bool kitty_keyboard = false;
            bool win32_input    = false;
            std::optional<bool> caps_lock;

            Vector2I mouse_position = Vector2I(0, 0);
            Vector2 mouse_movement  = Vector2(0, 0);

            uint32_t TranslateKey(Key key) const;

            void OpenKeyboardDevices();
            void SetupTerminalInput();
            void RestoreTerminalInput();

            void ReadTerminalInput();
            // returns the number of bytes consumed, 0 if the sequence is incomplete
            size_t ParseInput(const std::string& input, size_t start);
            size_t ParseCSI(const std::string& input, size_t start);
            void HandleCSI(const std::string& params, char final);
            void HandleWin32Key(const std::vector<int>& params);
            void HandleMouse(const std::vector<int>& params, char final);
            void HandleLegacyChar(unsigned char c);

            void PressKey(Key key, bool reports_release);
            void ReleaseKey(Key key);
            void ReleaseAllKeys();

            [[nodiscard]] bool UsesKeyboardDevices() const;
            [[nodiscard]] bool IsTerminalKeyHeld(Key key) const;

        public:
            LinuxInputManager();
            ~LinuxInputManager();

            virtual void UpdateInputEvents() override;

            Vector2I GetMousePosition() const override;
            void SetMousePosition(const Vector2I& position) override;

            Vector2 GetMouseDistanceToCenter() const override;
            void SetMousePositionToCenter() override;

            bool IsKeyPressed(Key key) const override;
            bool IsKeyReleased(Key key) const override;
            bool IsKeyHeld(Key key) const override;
        };
    }
}

#endif
#endif
