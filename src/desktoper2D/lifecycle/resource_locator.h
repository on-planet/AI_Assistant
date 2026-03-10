#pragma once

#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace desktoper2D {

class ResourceLocator {
public:
    static std::vector<std::filesystem::path> BuildSearchRoots(int max_depth = 12);

    static std::vector<std::string> BuildCandidatePaths(const std::string &relative_path,
                                                        int max_depth = 12);

    static std::vector<std::pair<std::string, std::string>> BuildCandidatePairs(const std::string &relative_left,
                                                                                const std::string &relative_right,
                                                                                int max_depth = 12);

    static std::vector<std::tuple<std::string, std::string, std::string>> BuildCandidateTriples(
        const std::string &relative_a,
        const std::string &relative_b,
        const std::string &relative_c,
        int max_depth = 12);

    static std::string ResolveFirstExisting(const std::string &relative_path,
                                            int max_depth = 12);
};

}  // namespace desktoper2D
