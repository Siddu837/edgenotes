#pragma once
#include <string>
#include <fstream>

namespace edgenotes {

enum class DockSide { Left, Right, Top, Bottom };

template <typename T>
inline T Clamp(T val, T minVal, T maxVal) {
    return (val < minVal) ? minVal : ((val > maxVal) ? maxVal : val);
}

struct Config {
    DockSide side = DockSide::Right;
    float position = 0.25f; // relative position along the screen edge (0.0 to 1.0)
    bool pinned = false;
    int themeMode = 0; // 0 = Auto (Windows system), 1 = Light, 2 = Dark
    bool hasRunBefore = false; // First-run detection for desktop shortcut prompt

    static std::string sideToString(DockSide s) {
        switch (s) {
            case DockSide::Left: return "left";
            case DockSide::Right: return "right";
            case DockSide::Top: return "top";
            case DockSide::Bottom: return "bottom";
        }
        return "right";
    }

    static DockSide stringToSide(const std::string& str) {
        if (str == "left") return DockSide::Left;
        if (str == "right") return DockSide::Right;
        if (str == "top") return DockSide::Top;
        if (str == "bottom") return DockSide::Bottom;
        return DockSide::Right;
    }

    bool load(const std::wstring& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        std::string line;
        while (std::getline(file, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // trim
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            val.erase(val.find_last_not_of(" \t\r\n") + 1);

            if (key == "side") side = stringToSide(val);
            else if (key == "position") {
                try { position = Clamp(std::stof(val), 0.0f, 1.0f); } catch (...) {}
            }
            else if (key == "pinned") pinned = (val == "1" || val == "true");
            else if (key == "theme") {
                try { themeMode = std::stoi(val); } catch (...) {}
            }
            else if (key == "hasRunBefore") hasRunBefore = (val == "1" || val == "true");
        }
        return true;
    }

    bool save(const std::wstring& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << "side=" << sideToString(side) << "\n";
        file << "position=" << position << "\n";
        file << "pinned=" << (pinned ? "1" : "0") << "\n";
        file << "theme=" << themeMode << "\n";
        file << "hasRunBefore=" << (hasRunBefore ? "1" : "0") << "\n";
        return true;
    }
};

} // namespace edgenotes
