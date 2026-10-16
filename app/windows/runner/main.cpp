#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <cstdio>

#include "flutter_window.h"
#include "utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 720);
  if (!window.Create(L"Choosen V2", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);
  // Development only: CHOOSEN_WINDOW_RECT="x,y,w,h" (physical pixels) opens the window there, e.g.
  // on a second monitor while tests run, and shows it without taking the focus. Flutter takes a new
  // monitor's scale from the DPI change of the move, which also rescales the window: so it moves
  // at the size that the rescale turns into w x h, then is set exactly (still hidden).
  char rect[64] = {};
  int x = 0, y = 0, w = 0, h = 0;
  if (::GetEnvironmentVariableA("CHOOSEN_WINDOW_RECT", rect, sizeof rect) > 0 &&
      sscanf_s(rect, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
    HWND hwnd = window.GetHandle();
    const int from = static_cast<int>(::GetDpiForWindow(hwnd));
    const int to = static_cast<int>(FlutterDesktopGetDpiForMonitor(::MonitorFromPoint({x, y}, MONITOR_DEFAULTTONEAREST)));
    ::SetWindowPos(hwnd, nullptr, x, y, ::MulDiv(w, from, to), ::MulDiv(h, from, to), SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    window.SetShowWithoutFocus(true);
  }

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
