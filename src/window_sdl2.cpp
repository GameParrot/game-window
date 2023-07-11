#include "window_sdl2.h"
#include "game_window_manager.h"
#include <string.h>
#include <string>         // std::string, std::wstring
#include <locale>         // std::wstring_convert
#include <codecvt>        // std::codecvt_utf8
#include <SDL2/SDL.h>

#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>

SDL2GameWindow::SDL2GameWindow(const std::string& title, int width, int height, GraphicsApi api) :
        GameWindow(title, width, height, api) {
    captured = false;
    initSDL();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(0, &mode);
    window = SDL_CreateWindow(title.data(), 0, 0, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    SDL_SetWindowDisplayMode(window, &mode);
    context = SDL_GL_CreateContext(window);
}

SDL2GameWindow::~SDL2GameWindow() {
    SDL_Quit();
}

void SDL2GameWindow::abortMsg(const char *msg)
{
    fflush(stdout);
    fprintf(stderr, "Fatal Error: %s\n", msg);
    exit(1);
}

void SDL2GameWindow::initSDL() {
    // video is mandatory to get events, even though we aren't using it, so we wont be creating a window
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER) != 0) {
        abortMsg("Unable to initialize SDL for video|events|gamecontroller");
    }

    if(!SDL_GameControllerEventState(SDL_QUERY))
        SDL_GameControllerEventState(SDL_ENABLE);

    gamepad.count = 0;
}

void SDL2GameWindow::setIcon(std::string const& iconPath) {
    // NOOP - borderless window
}

void SDL2GameWindow::makeCurrent(bool active) {
    SDL_GL_MakeCurrent(active ? window : nullptr, context);
}

void SDL2GameWindow::getWindowSize(int& width, int& height) const {
    SDL_GetWindowSize(window, &width, &height);
}

void SDL2GameWindow::show() {
    SDL_ShowWindow(window);
}

void SDL2GameWindow::close() {
    SDL_DestroyWindow(window);
}

void SDL2GameWindow::pollEvents() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                handleControllerDeviceEvent(&event.cdevice);
                break;
            case SDL_CONTROLLERAXISMOTION:
                handleControllerAxisEvent(&event.caxis);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                handleControllerButtonEvent(&event.cbutton);
                break;
            case SDL_MOUSEMOTION:
                handleMouseMotionEvent(&event.motion);
                break;
            case SDL_MOUSEWHEEL:
                handleMouseWheelEvent(&event.wheel);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                handleMouseClickEvent(&event.button);
                break;
            case SDL_KEYDOWN:
                handleKeyboardEvent(&event.key);
            case SDL_KEYUP:
                handleKeyboardEvent(&event.key);
                break;
            case SDL_QUIT:
                SDL_Quit();
                break;
            default:
                break;
        }
    }
}

void SDL2GameWindow::handleControllerDeviceEvent(SDL_ControllerDeviceEvent *cdeviceevent) {
    if (cdeviceevent->type == SDL_CONTROLLERDEVICEADDED) {
        // The game will only be informed of the first connection and last disconnection.
        // All inputs seen as controller 0 so the behaviour of multiple connected gamepads will be undefined.
        gamepad.count++;
        SDL_GameController *controller = NULL;
        controller = SDL_GameControllerOpen(cdeviceevent->which);
        if(!controller)
            printf("SDL2GameWindow: Couldn't open controller! - %s\n", SDL_GetError());
        else
            printf("SDL2GameWindow: Controller %d opened: %s!\n", cdeviceevent->which, SDL_GameControllerName(controller));
        if (gamepad.count > 1)
            return;
    }
    else if (cdeviceevent->type == SDL_CONTROLLERDEVICEREMOVED) {
        if (gamepad.count < 1) {
            printf("SDL2GameWindow: Error - controller removed when none were known to be connected");
            return;
        }
        else
            gamepad.count--;
            printf("SDL2GameWindow: Controller %d removed!\n", cdeviceevent->which);
            if (gamepad.count > 0)
                return;
    }
    else
        return;

    printf("SDL2GameWindow: There are now %d connected joysticks\n", SDL_NumJoysticks());
    onGamepadState(0, (cdeviceevent->type == SDL_CONTROLLERDEVICEADDED));
}

void SDL2GameWindow::handleControllerAxisEvent(SDL_ControllerAxisEvent *caxisevent) {
    GamepadAxisId axis;
    switch (caxisevent->axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            axis = GamepadAxisId::LEFT_X;
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            axis = GamepadAxisId::LEFT_Y;
            break;
        case SDL_CONTROLLER_AXIS_RIGHTX:
            axis = GamepadAxisId::RIGHT_X;
            break;
        case SDL_CONTROLLER_AXIS_RIGHTY:
            axis = GamepadAxisId::RIGHT_Y;
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            axis = GamepadAxisId::LEFT_TRIGGER;
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            axis = GamepadAxisId::RIGHT_TRIGGER;
            break;
        default :
            return;
    }

    double deflection = (double)caxisevent->value / 32768; // normalised -1 to 1 range
    onGamepadAxis(0, axis, deflection);
}

void SDL2GameWindow::handleControllerButtonEvent(SDL_ControllerButtonEvent *cbuttonevent) {
    GamepadButtonId btn;

    switch (cbuttonevent->button) {
        case SDL_CONTROLLER_BUTTON_A:
            btn = GamepadButtonId::A;
            break;
        case SDL_CONTROLLER_BUTTON_B:
            btn = GamepadButtonId::B;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            btn = GamepadButtonId::X;
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            btn = GamepadButtonId::Y;
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            btn = GamepadButtonId::BACK;
            break;
        case SDL_CONTROLLER_BUTTON_START:
            btn = GamepadButtonId::START;
            break;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            btn = GamepadButtonId::GUIDE;
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            btn = GamepadButtonId::LEFT_STICK;
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            btn = GamepadButtonId::RIGHT_STICK;
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            btn = GamepadButtonId::LB;
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            btn = GamepadButtonId::RB;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            btn = GamepadButtonId::DPAD_UP;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            btn = GamepadButtonId::DPAD_DOWN;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            btn = GamepadButtonId::DPAD_LEFT;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            btn = GamepadButtonId::DPAD_RIGHT;
            break;
        default :
            return;
    }

    onGamepadButton(0, btn, (cbuttonevent->state == SDL_PRESSED));
}

void SDL2GameWindow::handleMouseWheelEvent(SDL_MouseWheelEvent *wheelevent) {
    if (wheelevent->x < 0)
        onMouseScroll(wheelevent->x, wheelevent->y, -1.0, 0.0);
    else if (wheelevent->x > 0)
        onMouseScroll(wheelevent->x, wheelevent->y, 1.0, 0.0);
    else if (wheelevent->y < 0)
        onMouseScroll(0.0, 0.0, 0.0, -1);
    else if (wheelevent->y > 0)
        onMouseScroll(0.0, 0.0, 0.0, 1);
}

void SDL2GameWindow::handleMouseMotionEvent(SDL_MouseMotionEvent *motionevent) {
    if (captured) {
        onMouseRelativePosition(motionevent->xrel, motionevent->yrel);
    }
    else {
        onMousePosition(motionevent->x, motionevent->y);
    }
}

void SDL2GameWindow::handleMouseClickEvent(SDL_MouseButtonEvent *clickevent) {
    onMouseButton(clickevent->x, clickevent->y, clickevent->button, (clickevent->state == SDL_PRESSED ? MouseButtonAction::PRESS : MouseButtonAction::RELEASE));
}

void SDL2GameWindow::handleKeyboardEvent(SDL_KeyboardEvent *keyevent) {
    KeyCode key = getKeyMinecraft(keyevent->keysym.scancode);

    KeyAction action;
    if (keyevent->repeat) {
        action = KeyAction::REPEAT;
    }
    else {
        switch (keyevent->state) {
            case SDL_PRESSED: {
                action = KeyAction::PRESS;
                std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cvt;
                switch(key) {
                    case KeyCode::BACKSPACE:
                        onKeyboardText("\x08");
                    case KeyCode::DELETE:
                        onKeyboardText("\x7f");
                    case KeyCode::ENTER:
                        onKeyboardText("\n");
                    case KeyCode::LEFT:
                        onKeyboardText("\x1B");
                    default:
                        onKeyboardText(cvt.to_bytes((int)key));
                }
                break;
            };
            case SDL_RELEASED:
                action = KeyAction::RELEASE;
                break;
            default:
              return;
        }
    }

    onKeyboard(key, action);
}

void SDL2GameWindow::setCursorDisabled(bool disabled) {
    captured = disabled;
    SDL_CaptureMouse((SDL_bool) disabled);
}

void SDL2GameWindow::setFullscreen(bool fullscreen) {
    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

void SDL2GameWindow::setClipboardText(std::string const &text) {
    // NOOP - nowhere to cut/paste to/from without a desktop and other applications
}

void SDL2GameWindow::swapBuffers() {
    SDL_GL_SwapWindow(window);
}

void SDL2GameWindow::setSwapInterval(int interval) {
    SDL_GL_SetSwapInterval(interval);
}

// TODO fix QWERTY and numpad mapping.
KeyCode SDL2GameWindow::getKeyMinecraft(int keyCode) {
    int rKeyCode;

    switch (keyCode) {
        case SDL_SCANCODE_0:
            return KeyCode::NUM_0;
        case SDL_SCANCODE_1:
            return KeyCode::NUM_1;
        case SDL_SCANCODE_2:
            return KeyCode::NUM_2;
        case SDL_SCANCODE_3:
            return KeyCode::NUM_3;
        case SDL_SCANCODE_4:
            return KeyCode::NUM_4;
        case SDL_SCANCODE_5:
            return KeyCode::NUM_5;
        case SDL_SCANCODE_6:
            return KeyCode::NUM_6;
        case SDL_SCANCODE_7:
            return KeyCode::NUM_7;
        case SDL_SCANCODE_8:
            return KeyCode::NUM_8;
        case SDL_SCANCODE_9:
            return KeyCode::NUM_9;
        case SDL_SCANCODE_A ... SDL_SCANCODE_Z:
            return (KeyCode)(keyCode+61);
        case SDL_SCANCODE_BACKSLASH:
            return KeyCode::BACKSLASH;
        case SDL_SCANCODE_AC_HOME:
            return KeyCode::HOME;
        case SDL_SCANCODE_AC_BACK:
            return KeyCode::BACK;
        case SDL_SCANCODE_BACKSPACE:
            return KeyCode::BACKSPACE;
        case SDL_SCANCODE_CAPSLOCK:
            return KeyCode::CAPS_LOCK;
        case SDL_SCANCODE_COMMA:
            return KeyCode::COMMA;
        case SDL_SCANCODE_DELETE:
            return KeyCode::DELETE;
        case SDL_SCANCODE_DOWN:
            return KeyCode::DOWN;
        case SDL_SCANCODE_END:
            return KeyCode::END;
        case SDL_SCANCODE_EQUALS:
            return KeyCode::EQUAL;
        case SDL_SCANCODE_ESCAPE:
            return KeyCode::ESCAPE;
        case SDL_SCANCODE_HOME:
            return KeyCode::HOME;
        case SDL_SCANCODE_INSERT:
            return KeyCode::INSERT;
        case SDL_SCANCODE_KP_0:
            return KeyCode::NUMPAD_0;
        case SDL_SCANCODE_KP_1:
            return KeyCode::NUMPAD_1;
        case SDL_SCANCODE_KP_2:
            return KeyCode::NUMPAD_2;
        case SDL_SCANCODE_KP_3:
            return KeyCode::NUMPAD_3;
        case SDL_SCANCODE_KP_4:
            return KeyCode::NUMPAD_4;
        case SDL_SCANCODE_KP_5:
            return KeyCode::NUMPAD_5;
        case SDL_SCANCODE_KP_6:
            return KeyCode::NUMPAD_6;
        case SDL_SCANCODE_KP_7:
            return KeyCode::NUMPAD_7;
        case SDL_SCANCODE_KP_8:
            return KeyCode::NUMPAD_8;
        case SDL_SCANCODE_KP_9:
            return KeyCode::NUMPAD_9;
        case SDL_SCANCODE_KP_MINUS:
            return KeyCode::NUMPAD_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS:
            return KeyCode::NUMPAD_ADD;
        case SDL_SCANCODE_LALT:
            return KeyCode::LEFT_ALT;
        case SDL_SCANCODE_LCTRL:
            return KeyCode::LEFT_CTRL;
        case SDL_SCANCODE_LEFT:
            return KeyCode::LEFT;
        case SDL_SCANCODE_LEFTBRACKET:
            return KeyCode::LEFT_BRACKET;
        case SDL_SCANCODE_LSHIFT:
            return KeyCode::LEFT_SHIFT;
        case SDL_SCANCODE_MENU:
            return KeyCode::MENU;
        case SDL_SCANCODE_MINUS:
            return KeyCode::MINUS;
        case SDL_SCANCODE_PAGEDOWN:
            return KeyCode::PAGE_DOWN;
        case SDL_SCANCODE_PAGEUP:
            return KeyCode::PAGE_UP;
        case SDL_SCANCODE_PERIOD:
            return KeyCode::PERIOD;
        case SDL_SCANCODE_RALT:
            return KeyCode::RIGHT_ALT;
        case SDL_SCANCODE_RCTRL:
            return KeyCode::RIGHT_CTRL;
        case SDL_SCANCODE_RIGHT:
            return KeyCode::RIGHT;
        case SDL_SCANCODE_RIGHTBRACKET:
            return KeyCode::RIGHT_BRACKET;
        case SDL_SCANCODE_RSHIFT:
            return KeyCode::RIGHT_SHIFT;
        case SDL_SCANCODE_SEMICOLON:
            return KeyCode::SEMICOLON;
        case SDL_SCANCODE_SLASH:
            return KeyCode::SLASH;
        case SDL_SCANCODE_SPACE:
            return KeyCode::SPACE;
        case SDL_SCANCODE_TAB:
            return KeyCode::TAB;
        
        
    }
        return KeyCode::UNKNOWN;
}
