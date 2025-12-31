#pragma once
#include "Style.h"
#include <string>
#include <unordered_map>

class CssParser {
public:
    std::unordered_map<std::string, ModuleStyle> ParseFile(const std::string &filename);

private:
    ModuleStyle ParseProperties(const std::unordered_map<std::string, std::string> &props);
};
