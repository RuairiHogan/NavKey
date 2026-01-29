#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include "UIElementScanner.h"

namespace hint_map {

	void ShowHintOverlay(HINSTANCE hInstance, const std::vector<HintTarget>& hintTargets, const std::vector<std::wstring>& labels);
	void CloseHintOverlay();
	std::vector<std::wstring> GenerateHintLabels(int count);

}
