#include "pch.h"
#include "Util.h"
#include "globals.h"

std::string Util::getEnvironmentVariable(const std::string& varName)
{
    if (const char* value = std::getenv(varName.data()))
    {
        return {value};
    }
    return "";
}

int32 Util::executeCommand(const std::string& command)
{
    return std::system(command.data());
}

std::vector<std::filesystem::path> Util::getAllFilesInDirectory(const std::filesystem::path& dirPath, const std::vector<std::string>& extensions)
{
    namespace fs = std::filesystem;

    std::vector<std::filesystem::path> paths;
    
    if (fs::exists(dirPath) && fs::is_directory(dirPath))
    {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath))
        {
            if (fs::is_regular_file(entry))
            {
                if (!extensions.empty())
                {
                    if (std::find(extensions.begin(), extensions.end(), entry.path().extension().string()) != extensions.end())
                    {
                        paths.push_back(entry); 
                    }
                }
                else
                {
                    paths.push_back(entry);   
                }
            }
        }
    }
    return paths;
}

std::vector<char> Util::readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        DEBUG_BREAK
        LLogger::LogString(std::format("Failed to open file: {}", filename));
        return {};
    }

    uint64 fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
