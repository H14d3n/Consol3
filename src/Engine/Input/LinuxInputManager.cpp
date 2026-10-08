#ifdef SYS_LINUX
#include "LinuxInputManager.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Engine
{
    namespace Input
    {
        // terminals without release events only send a key press followed by the key repeat, the first repeat comes after a delay
        // (usually 250-660ms), so a new press counts as held for longer than that, and every repeat extends it a bit
        static constexpr int64_t FIRST_PRESS_HOLD_MS = 700;
        static constexpr int64_t REPEAT_HOLD_MS      = 150;
        // an escape sequence that is still incomplete after this long was a lone escape key press, or garbage
        static constexpr int64_t INCOMPLETE_INPUT_TIMEOUT_MS = 100;

        // kitty keyboard protocol: disambiguate (1) + report press/repeat/release (2) + report every key as an escape code (8), then query support
        // win32-input-mode (Windows Terminal), mouse buttons + any motion in SGR encoding, focus in/out
        static constexpr char ENABLE_INPUT_SEQUENCE[]  = "\033[>11u\033[?u\033[?9001h\033[?1000h\033[?1002h\033[?1003h\033[?1006h\033[?1004h";
        static constexpr char DISABLE_INPUT_SEQUENCE[] = "\033[?1004l\033[?1006l\033[?1003l\033[?1002l\033[?1000l\033[?9001l\033[<u";

        // kitty keyboard protocol key codes for keys outside of the unicode range
        static constexpr int KITTY_CAPS_LOCK     = 57358;
        static constexpr int KITTY_LEFT_SHIFT    = 57441;
        static constexpr int KITTY_LEFT_CONTROL  = 57442;
        static constexpr int KITTY_RIGHT_SHIFT   = 57447;
        static constexpr int KITTY_RIGHT_CONTROL = 57448;

        static termios saved_termios;
        static bool saved_termios_valid = false;
        static void (*previous_sigint_handler)(int)  = SIG_DFL;
        static void (*previous_sigterm_handler)(int) = SIG_DFL;

        static void RestoreInputAndExit(int signal)
        {
            // only async-signal-safe calls in here
            (void)!write(STDOUT_FILENO, DISABLE_INPUT_SEQUENCE, sizeof(DISABLE_INPUT_SEQUENCE) - 1);

            if (saved_termios_valid)
            {
                tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
                // don't leave unread input (e.g. mouse reports) behind for the shell
                tcflush(STDIN_FILENO, TCIFLUSH);
            }

            // let the terminal manager restore the display as well
            void (*previous_handler)(int) = signal == SIGINT ? previous_sigint_handler : previous_sigterm_handler;
            if (previous_handler != SIG_DFL && previous_handler != SIG_IGN && previous_handler != SIG_ERR && previous_handler != RestoreInputAndExit)
                previous_handler(signal);

            _exit(128 + signal);
        }

        static int64_t GetCurrentTimeMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        static std::optional<Key> KeyFromChar(int c)
        {
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';

            switch (c)
            {
            case 'q':
                return Key::Q;
            case 'w':
                return Key::W;
            case 'e':
                return Key::E;
            case 'a':
                return Key::A;
            case 's':
                return Key::S;
            case 'd':
                return Key::D;
            case 'p':
                return Key::P;
            case 'r':
                return Key::R;
            case 't':
                return Key::T;
            case 'f':
                return Key::F;
            case 'g':
                return Key::G;
            case 'h':
                return Key::H;
            case ' ':
                return Key::SPACE;
            case '\t':
                return Key::TAB;
            case '1':
                return Key::N1;
            case '2':
                return Key::N2;
            case '3':
                return Key::N3;
            case '4':
                return Key::N4;
            case '5':
                return Key::N5;
            case '6':
                return Key::N6;
            case '7':
                return Key::N7;
            case '8':
                return Key::N8;
            case '9':
                return Key::N9;
            case '0':
                return Key::N0;
            }
            return std::nullopt;
        }

        // splits "1;2:3;;4" into {{1}, {2, 3}, {-1}, {4}}, missing values are -1
        static std::vector<std::vector<int>> ParseParams(const std::string& params)
        {
            std::vector<std::vector<int>> fields(1, std::vector<int>(1, -1));

            for (char c : params)
            {
                if (c == ';')
                    fields.emplace_back(1, -1);
                else if (c == ':')
                    fields.back().push_back(-1);
                else if (c >= '0' && c <= '9')
                    fields.back().back() = std::max(fields.back().back(), 0) * 10 + (c - '0');
            }

            return fields;
        }

        static int GetParam(const std::vector<std::vector<int>>& fields, size_t field, size_t sub, int default_value)
        {
            if (field >= fields.size() || sub >= fields[field].size() || fields[field][sub] < 0)
                return default_value;

            return fields[field][sub];
        }

        static bool IsMouseKey(Key key)
        {
            return key == Key::MOUSE1 || key == Key::MOUSE2 || key == Key::MOUSE3 || key == Key::MOUSE4 || key == Key::MOUSE5;
        }

        LinuxInputManager::LinuxInputManager()
        {
            std::fill(keyboard_state, keyboard_state + KEY_MAX / 8 + 1, 0);
            std::fill(keyboard_leds, keyboard_leds + LED_MAX / 8 + 1, 0);

            OpenKeyboardDevices();
            SetupTerminalInput();
        }

        LinuxInputManager::~LinuxInputManager()
        {
            RestoreTerminalInput();

            for (int keyboard_device_file : keyboard_device_files)
                close(keyboard_device_file);
        }

        void LinuxInputManager::OpenKeyboardDevices()
        {
            // over ssh the physical keyboard belongs to someone else (or no one)
            if (std::getenv("SSH_CONNECTION") != nullptr || std::getenv("SSH_TTY") != nullptr)
                return;

            std::set<std::filesystem::path> opened_devices;

            for (const char* directory : {"/dev/input/by-path", "/dev/input/by-id"})
            {
                std::error_code error;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
                {
                    if (!entry.path().filename().string().ends_with("-event-kbd"))
                        continue;

                    // the same device is usually listed in both directories
                    std::filesystem::path device = std::filesystem::canonical(entry.path(), error);
                    if (error || !opened_devices.insert(device).second)
                        continue;

                    // fails without read access, which is the usual case for non root users
                    int device_file = open(device.c_str(), O_RDONLY);
                    if (device_file >= 0)
                        keyboard_device_files.push_back(device_file);
                }
            }
        }

        void LinuxInputManager::SetupTerminalInput()
        {
            if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &original_termios) != 0)
                return;

            termios raw = original_termios;
            // read every byte as it comes in without echoing it, and without the terminal driver interpreting flow control or carriage returns
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_iflag &= ~(IXON | ICRNL);
            // non blocking reads
            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 0;
            // Ctrl+C still quits, but Ctrl+Z and Ctrl+\ would leave the terminal in this mode
            raw.c_cc[VSUSP] = _POSIX_VDISABLE;
            raw.c_cc[VQUIT] = _POSIX_VDISABLE;

            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
                return;

            terminal_input = true;

            saved_termios       = original_termios;
            saved_termios_valid = true;

            previous_sigint_handler  = std::signal(SIGINT, RestoreInputAndExit);
            previous_sigterm_handler = std::signal(SIGTERM, RestoreInputAndExit);

            std::cout << ENABLE_INPUT_SEQUENCE << std::flush;
        }

        void LinuxInputManager::RestoreTerminalInput()
        {
            if (!terminal_input)
                return;

            std::cout << DISABLE_INPUT_SEQUENCE << std::flush;

            tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
            tcflush(STDIN_FILENO, TCIFLUSH);

            std::signal(SIGINT, previous_sigint_handler);
            std::signal(SIGTERM, previous_sigterm_handler);

            saved_termios_valid = false;
            terminal_input      = false;
        }

        void LinuxInputManager::UpdateInputEvents()
        {
            if (UsesKeyboardDevices())
            {
                // clear state
                std::fill(keyboard_state, keyboard_state + KEY_MAX / 8 + 1, 0);
                std::fill(keyboard_leds, keyboard_leds + LED_MAX / 8 + 1, 0);

                // combine all keyboards
                for (int keyboard_device_file : keyboard_device_files)
                {
                    uint8_t device_state[KEY_MAX / 8 + 1] = {};
                    uint8_t device_leds[LED_MAX / 8 + 1]  = {};

                    ioctl(keyboard_device_file, EVIOCGKEY(sizeof(device_state)), device_state);
                    ioctl(keyboard_device_file, EVIOCGLED(sizeof(device_leds)), device_leds);

                    for (size_t i = 0; i < sizeof(device_state); i++)
                        keyboard_state[i] |= device_state[i];
                    for (size_t i = 0; i < sizeof(device_leds); i++)
                        keyboard_leds[i] |= device_leds[i];
                }
            }

            // always read the terminal, even when using the keyboard devices: it has the mouse, and unread input would end up in the shell
            if (terminal_input)
                ReadTerminalInput();
        }

        void LinuxInputManager::ReadTerminalInput()
        {
            for (TerminalKeyState& state : terminal_keys)
            {
                if (state.release_on_next_update)
                    state.down = false;

                state.pressed_this_update    = false;
                state.release_on_next_update = false;
            }

            char buffer[4096];
            ssize_t read_count;

            while ((read_count = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
                pending_input.append(buffer, read_count);

            size_t position = 0;
            while (position < pending_input.size())
            {
                size_t consumed = ParseInput(pending_input, position);
                if (consumed == 0)
                    break;

                position += consumed;
            }

            pending_input.erase(0, position);

            if (pending_input.empty())
            {
                pending_input_time = 0;
            }
            else if (pending_input_time == 0)
            {
                pending_input_time = GetCurrentTimeMs();
            }
            else if (GetCurrentTimeMs() - pending_input_time > INCOMPLETE_INPUT_TIMEOUT_MS)
            {
                pending_input.clear();
                pending_input_time = 0;
            }
        }

        size_t LinuxInputManager::ParseInput(const std::string& input, size_t start)
        {
            unsigned char c = input[start];

            if (c != '\033')
            {
                HandleLegacyChar(c);
                return 1;
            }

            if (start + 1 >= input.size())
                return 0;

            char next = input[start + 1];

            if (next == '[')
                return ParseCSI(input, start);

            // SS3, sent for the arrow keys by terminals in application cursor mode
            if (next == 'O')
            {
                if (start + 2 >= input.size())
                    return 0;

                HandleCSI("", input[start + 2]);
                return 3;
            }

            // alt + key
            HandleLegacyChar(static_cast<unsigned char>(next));
            return 2;
        }

        size_t LinuxInputManager::ParseCSI(const std::string& input, size_t start)
        {
            // parameter and intermediate bytes, then a single final byte
            size_t index = start + 2;
            while (index < input.size() && input[index] >= 0x20 && input[index] <= 0x3F)
                index++;

            if (index >= input.size())
                return 0;

            char final = input[index];

            // malformed, skip it
            if (final < 0x40 || final > 0x7E)
                return index - start;

            HandleCSI(input.substr(start + 2, index - start - 2), final);

            return index - start + 1;
        }

        void LinuxInputManager::HandleCSI(const std::string& params, char final)
        {
            if (!params.empty() && params[0] == '<')
            {
                if (final == 'M' || final == 'm')
                {
                    std::vector<std::vector<int>> fields = ParseParams(params.substr(1));
                    HandleMouse({GetParam(fields, 0, 0, 0), GetParam(fields, 1, 0, 1), GetParam(fields, 2, 0, 1)}, final);
                }
                return;
            }

            if (!params.empty() && params[0] == '?')
            {
                // reply to the kitty keyboard protocol query, the terminal supports it
                if (final == 'u')
                    kitty_keyboard = true;
                // anything else is a reply we didn't ask for (e.g. device attributes)
                return;
            }

            std::vector<std::vector<int>> fields = ParseParams(params);

            if (final == '_')
            {
                std::vector<int> win32_params;
                for (size_t i = 0; i < 6; i++)
                    win32_params.push_back(GetParam(fields, i, 0, -1));

                HandleWin32Key(win32_params);
                return;
            }

            if (params.empty() && final == 'I')
                return;
            // focus lost, the releases of keys held at that moment will go somewhere else
            if (params.empty() && final == 'O')
            {
                ReleaseAllKeys();
                return;
            }

            int code       = GetParam(fields, 0, 0, 1);
            int modifiers  = GetParam(fields, 1, 0, 1) - 1;
            int event_type = GetParam(fields, 1, 1, 1);

            std::optional<Key> key;

            switch (final)
            {
            case 'A':
                key = Key::UP_ARROW;
                break;
            case 'B':
                key = Key::DOWN_ARROW;
                break;
            case 'C':
                key = Key::RIGHT_ARROW;
                break;
            case 'D':
                key = Key::LEFT_ARROW;
                break;
            case '~':
                if (code == 5)
                    key = Key::PAGE_UP;
                else if (code == 6)
                    key = Key::PAGE_DOWN;
                break;
            case 'u':
                if (code == KITTY_CAPS_LOCK)
                    key = Key::CAPITAL;
                else if (code == KITTY_LEFT_SHIFT || code == KITTY_RIGHT_SHIFT)
                    key = Key::LSHIFT;
                else if (code == KITTY_LEFT_CONTROL || code == KITTY_RIGHT_CONTROL)
                    key = Key::LCONTROL;
                else
                    key = KeyFromChar(code);
                break;
            }

            // only the kitty keyboard protocol uses 'u', the other keys have the same encoding with and without it
            bool reports_release = kitty_keyboard || final == 'u';

            if (reports_release)
            {
                // with the kitty protocol Ctrl+C is reported like any other key instead of sending SIGINT
                if (final == 'u' && code == 'c' && (modifiers & 4) && event_type != 3)
                    std::raise(SIGINT);

                if (GetParam(fields, 1, 0, -1) != -1)
                    caps_lock = (modifiers & 64) != 0;
            }
            else
            {
                // without the kitty protocol modifiers are only known when pressed together with another key
                if (modifiers & 1)
                    PressKey(Key::LSHIFT, false);
                if (modifiers & 4)
                    PressKey(Key::LCONTROL, false);
            }

            if (!key)
                return;

            if (event_type == 3)
                ReleaseKey(*key);
            else
                PressKey(*key, reports_release);
        }

        void LinuxInputManager::HandleWin32Key(const std::vector<int>& params)
        {
            // CSI Vk ; Sc ; Uc ; Kd ; Cs ; Rc _
            int virtual_key   = std::max(params[0], 0);
            int unicode_char  = std::max(params[2], 0);
            bool key_down     = params[3] == 1;
            int control_state = std::max(params[4], 0);

            win32_input = true;

            // CAPSLOCK_ON
            caps_lock = (control_state & 0x80) != 0;

            // in win32-input-mode Ctrl+C is reported like any other key instead of sending SIGINT
            if (key_down && (unicode_char == 3 || (virtual_key == 'C' && (control_state & 0x0C))))
                std::raise(SIGINT);

            std::optional<Key> key;

            switch (virtual_key)
            {
            case 0x25:
                key = Key::LEFT_ARROW;
                break;
            case 0x26:
                key = Key::UP_ARROW;
                break;
            case 0x27:
                key = Key::RIGHT_ARROW;
                break;
            case 0x28:
                key = Key::DOWN_ARROW;
                break;
            case 0x21:
                key = Key::PAGE_UP;
                break;
            case 0x22:
                key = Key::PAGE_DOWN;
                break;
            case 0x14:
                key = Key::CAPITAL;
                break;
            // VK_SHIFT, VK_LSHIFT, VK_RSHIFT
            case 0x10:
            case 0xA0:
            case 0xA1:
                key = Key::LSHIFT;
                break;
            // VK_CONTROL, VK_LCONTROL, VK_RCONTROL
            case 0x11:
            case 0xA2:
            case 0xA3:
                key = Key::LCONTROL;
                break;
            default:
                // the virtual key codes of letters, numbers, space and tab are their ascii values
                key = KeyFromChar(virtual_key);
                break;
            }

            if (!key)
                return;

            if (key_down)
                PressKey(*key, true);
            else
                ReleaseKey(*key);
        }

        void LinuxInputManager::HandleMouse(const std::vector<int>& params, char final)
        {
            // CSI < button ; x ; y M (press/motion) or m (release), coordinates start at 1
            int button = params[0];

            Vector2I new_position = Vector2I(params[1] - 1, params[2] - 1);
            mouse_movement.x += static_cast<float>(new_position.x - mouse_position.x);
            mouse_movement.y += static_cast<float>(new_position.y - mouse_position.y);
            mouse_position = new_position;

            bool extra_button = button & 128;
            // wheel and motion don't change the button state
            if ((!extra_button && (button & 64)) || (button & 32))
                return;

            std::optional<Key> key;

            switch ((extra_button ? 8 : 0) + (button & 3))
            {
            case 0:
                key = Key::MOUSE1;
                break;
            case 1:
                key = Key::MOUSE3;
                break;
            case 2:
                key = Key::MOUSE2;
                break;
            // back and forward, same as XBUTTON1 and XBUTTON2 on windows
            case 8:
                key = Key::MOUSE4;
                break;
            case 9:
                key = Key::MOUSE5;
                break;
            }

            if (!key)
                return;

            if (final == 'M')
                PressKey(*key, true);
            else
                ReleaseKey(*key);
        }

        void LinuxInputManager::HandleLegacyChar(unsigned char c)
        {
            // only reaches here if the terminal driver didn't already turn it into SIGINT
            if (c == 3)
            {
                std::raise(SIGINT);
                return;
            }

            // Ctrl+Space
            if (c == 0)
            {
                PressKey(Key::LCONTROL, false);
                PressKey(Key::SPACE, false);
                return;
            }

            // Ctrl+letter, except the ones that are also tab and enter
            if (c >= 1 && c <= 26 && c != '\t' && c != '\n' && c != '\r')
            {
                PressKey(Key::LCONTROL, false);
                c = 'a' + c - 1;
            }

            if (c >= 'A' && c <= 'Z')
                PressKey(Key::LSHIFT, false);

            std::optional<Key> key = KeyFromChar(c);
            if (key)
                PressKey(*key, false);
        }

        void LinuxInputManager::PressKey(Key key, bool reports_release)
        {
            TerminalKeyState& state = terminal_keys[static_cast<size_t>(key)];
            bool was_held           = IsTerminalKeyHeld(key);

            if (!was_held)
                state.toggled = !state.toggled;

            if (reports_release)
            {
                state.down                   = true;
                state.pressed_this_update    = true;
                state.release_on_next_update = false;
            }
            else
                state.held_until = GetCurrentTimeMs() + (was_held ? REPEAT_HOLD_MS : FIRST_PRESS_HOLD_MS);
        }

        void LinuxInputManager::ReleaseKey(Key key)
        {
            TerminalKeyState& state = terminal_keys[static_cast<size_t>(key)];

            if (state.pressed_this_update)
            {
                state.release_on_next_update = true;
                return;
            }

            state.down       = false;
            state.held_until = 0;
        }

        void LinuxInputManager::ReleaseAllKeys()
        {
            for (TerminalKeyState& state : terminal_keys)
            {
                state.down                   = false;
                state.held_until             = 0;
                state.pressed_this_update    = false;
                state.release_on_next_update = false;
            }
        }

        bool LinuxInputManager::UsesKeyboardDevices() const
        {
            return !keyboard_device_files.empty();
        }

        bool LinuxInputManager::IsTerminalKeyHeld(Key key) const
        {
            const TerminalKeyState& state = terminal_keys[static_cast<size_t>(key)];

            return state.down || GetCurrentTimeMs() < state.held_until;
        }

        uint32_t LinuxInputManager::TranslateKey(Key key) const
        {
            switch (key)
            {
            case Key::LEFT_ARROW:
                return KEY_LEFT;
            case Key::RIGHT_ARROW:
                return KEY_RIGHT;
            case Key::UP_ARROW:
                return KEY_UP;
            case Key::DOWN_ARROW:
                return KEY_DOWN;
            case Key::CAPITAL:
                return KEY_CAPSLOCK;
            case Key::SPACE:
                return KEY_SPACE;
            case Key::LCONTROL:
                return KEY_LEFTCTRL;
            case Key::LSHIFT:
                return KEY_LEFTSHIFT;
            case Key::Q:
                return KEY_Q;
            case Key::W:
                return KEY_W;
            case Key::E:
                return KEY_E;
            case Key::A:
                return KEY_A;
            case Key::S:
                return KEY_S;
            case Key::D:
                return KEY_D;
            case Key::P:
                return KEY_P;
            case Key::R:
                return KEY_R;
            case Key::T:
                return KEY_T;
            case Key::F:
                return KEY_F;
            case Key::G:
                return KEY_G;
            case Key::H:
                return KEY_H;
            case Key::TAB:
                return KEY_TAB;
            case Key::MOUSE1:
                return BTN_LEFT;
            case Key::MOUSE2:
                return BTN_RIGHT;
            case Key::MOUSE3:
                return BTN_MIDDLE;
            case Key::MOUSE4:
                return BTN_FORWARD;
            case Key::MOUSE5:
                return BTN_BACK;
            case Key::N1:
                return KEY_1;
            case Key::N2:
                return KEY_2;
            case Key::N3:
                return KEY_3;
            case Key::N4:
                return KEY_4;
            case Key::N5:
                return KEY_5;
            case Key::N6:
                return KEY_6;
            case Key::N7:
                return KEY_7;
            case Key::N8:
                return KEY_8;
            case Key::N9:
                return KEY_9;
            case Key::N0:
                return KEY_0;
            case Key::PAGE_UP:
                return KEY_PAGEUP;
            case Key::PAGE_DOWN:
                return KEY_PAGEDOWN;
            case Key::KEY_COUNT:
                break;
            }
            return 0;
        }

        Vector2I LinuxInputManager::GetMousePosition() const
        {
            // in terminal cells
            return mouse_position;
        }

        void LinuxInputManager::SetMousePosition(const Vector2I& position)
        {
            // a terminal can't move the mouse
        }

        Vector2 LinuxInputManager::GetMouseDistanceToCenter() const
        {
            // the mouse can't be moved back to the center, so this is the movement since the last SetMousePositionToCenter instead,
            // converted from cells to pixels so the sensitivity is close to a real mouse
            float cell_width  = 8.0f;
            float cell_height = 16.0f;

            winsize size{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_xpixel != 0 && size.ws_ypixel != 0 && size.ws_col != 0 && size.ws_row != 0)
            {
                cell_width  = static_cast<float>(size.ws_xpixel) / size.ws_col;
                cell_height = static_cast<float>(size.ws_ypixel) / size.ws_row;
            }

            return Vector2(mouse_movement.x * cell_width, mouse_movement.y * cell_height);
        }

        void LinuxInputManager::SetMousePositionToCenter()
        {
            mouse_movement = Vector2(0, 0);
        }

        bool LinuxInputManager::IsKeyPressed(Key key) const
        {
            // same as windows: whether the key is toggled on, only really meaningful for caps lock
            if (key == Key::CAPITAL)
            {
                if (UsesKeyboardDevices())
                    return keyboard_leds[LED_CAPSL / 8] & (1 << (LED_CAPSL % 8));
                if (caps_lock)
                    return *caps_lock;
            }

            return terminal_keys[static_cast<size_t>(key)].toggled;
        }

        bool LinuxInputManager::IsKeyReleased(Key key) const
        {
            return !IsKeyHeld(key);
        }

        bool LinuxInputManager::IsKeyHeld(Key key) const
        {
            // the keyboard devices don't have the mouse, it always comes from the terminal
            if (IsMouseKey(key) || !UsesKeyboardDevices())
                return IsTerminalKeyHeld(key);

            uint32_t linux_key = TranslateKey(key);
            uint16_t key_index = linux_key / 8;
            uint16_t held_mask = 1 << (linux_key % 8);

            return keyboard_state[key_index] & held_mask;
        }

    }
}
#endif
