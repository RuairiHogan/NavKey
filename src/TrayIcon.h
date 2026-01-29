#pragma once
#include <Windows.h>

void InitTrayIcon(HINSTANCE hInstance);
void CleanupTrayIcon();
extern bool app_is_running;
