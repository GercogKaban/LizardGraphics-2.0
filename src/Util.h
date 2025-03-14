#pragma once

#include "globals.h"
#include <string>
#include <vector>
#include <filesystem>

class Util
{
public:
    
    static std::string getEnvironmentVariable(const std::string& varName);
    static int32 executeCommand(const std::string& command);
    static std::vector<std::filesystem::path> getAllFilesInDirectory(const std::filesystem::path& directory, const std::vector<std::string>& extensions = {});
    static std::vector<char> readFile(const std::string& filename);
};