#ifndef ENGINE_ASSET_SHADER_SHADERINCLUDER_H
#define ENGINE_ASSET_SHADER_SHADERINCLUDER_H

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <glslang/Public/ShaderLang.h>

namespace Engine {
    // Default include class for normal include convention of search backward
    // through the stack of active include paths (for nested includes).
    // Can be overridden to customize.
    // reference: glslang/StandAlone/DirStackFileIncluder.h
    class DirStackFileIncluder : public glslang::TShader::Includer {
    public:
        DirStackFileIncluder();

        virtual IncludeResult *includeLocal(
            const char *headerName, const char *includerName, size_t inclusionDepth
        ) override;

        virtual IncludeResult *includeSystem(
            const char *headerName, const char *includerName, size_t inclusionDepth
        ) override;

        // Externally set directories. E.g., from a command-line -I<dir>.
        //  - Most-recently pushed are checked first.
        //  - All these are checked after the parse-time stack of local directories
        //    is checked.
        //  - This only applies to the "local" form of #include.
        //  - Makes its own copy of the path.

        virtual void releaseInclude(IncludeResult *result) override;
        virtual std::set<std::string> getIncludedFiles();
        virtual ~DirStackFileIncluder() override;

        void setLocalPath(const std::filesystem::path &path);
        void addSystemPath(const std::filesystem::path &path);

    protected:
        typedef char tUserDataElement;
        std::filesystem::path localPath{};
        std::vector<std::filesystem::path> directoryStack{};
        std::vector<std::filesystem::path> systemPaths{};
        std::set<std::string> includedFiles{};

        // Search for a valid "local" path based on combining the stack of include
        // directories and the nominal name of the header.
        virtual IncludeResult *readLocalPath(const char *headerName, const char *includerName, int depth);

        // Search for a valid <system> path.
        virtual IncludeResult *readSystemPath(const char *headerName) const;

        // Do actual reading of the file, filling in a new include result.
        virtual IncludeResult *newIncludeResult(const std::string &path, std::ifstream &file, int length) const;
    };

} // namespace Engine

#endif // ENGINE_ASSET_SHADER_SHADERINCLUDER_H
