module;
#include <ctime>
module aether.core;

import <iostream>;
import <chrono>;
import <string_view>;

namespace aether::log {

void write(Level level, std::string_view message) {
    static const char* levelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << "[" << levelNames[static_cast<int>(level)] << "] ";
    // Print timestamp
    char timebuf[32];
    struct tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &localTime);
    std::cout << timebuf << " " << message << std::endl;
}

} // namespace aether::log
