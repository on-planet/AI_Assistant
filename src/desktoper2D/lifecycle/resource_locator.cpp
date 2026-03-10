#include "desktoper2D/lifecycle/resource_locator.h"

#include <algorithm>
#include <system_error>

namespace desktoper2D {

namespace {

std::filesystem::path NormalizePath(const std::filesystem::path &path) {
    return path.lexically_normal();
}

}  // namespace

std::vector<std::filesystem::path> ResourceLocator::BuildSearchRoots(int max_depth) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(static_cast<std::size_t>(std::max(0, max_depth)) + 3);

#ifdef K2D_PROJECT_DIR
    roots.emplace_back(std::filesystem::path(K2D_PROJECT_DIR));
#endif

    {
        std::error_code ec;
        const auto cwd = std::filesystem::current_path(ec);
        if (!ec) {
            roots.emplace_back(cwd);
        }
    }

    std::filesystem::path prefix;
    for (int i = 0; i <= max_depth; ++i) {
        roots.emplace_back(prefix);
        prefix /= "..";
    }

    for (auto &root : roots) {
        root = NormalizePath(root);
    }

    return roots;
}

std::vector<std::string> ResourceLocator::BuildCandidatePaths(const std::string &relative_path,
                                                              int max_depth) {
    std::vector<std::string> out;
    if (relative_path.empty()) {
        return out;
    }

    const auto roots = BuildSearchRoots(max_depth);
    out.reserve(roots.size());

    for (const auto &root : roots) {
        const auto candidate = NormalizePath(root / relative_path);
        out.push_back(candidate.generic_string());
    }

    return out;
}

std::vector<std::pair<std::string, std::string>> ResourceLocator::BuildCandidatePairs(
    const std::string &relative_left,
    const std::string &relative_right,
    int max_depth) {
    std::vector<std::pair<std::string, std::string>> out;
    if (relative_left.empty() || relative_right.empty()) {
        return out;
    }

    const auto roots = BuildSearchRoots(max_depth);
    out.reserve(roots.size());

    for (const auto &root : roots) {
        const auto left = NormalizePath(root / relative_left);
        const auto right = NormalizePath(root / relative_right);
        out.emplace_back(left.generic_string(), right.generic_string());
    }

    return out;
}

std::vector<std::tuple<std::string, std::string, std::string>> ResourceLocator::BuildCandidateTriples(
    const std::string &relative_a,
    const std::string &relative_b,
    const std::string &relative_c,
    int max_depth) {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    if (relative_a.empty() || relative_b.empty() || relative_c.empty()) {
        return out;
    }

    const auto roots = BuildSearchRoots(max_depth);
    out.reserve(roots.size());

    for (const auto &root : roots) {
        const auto a = NormalizePath(root / relative_a);
        const auto b = NormalizePath(root / relative_b);
        const auto c = NormalizePath(root / relative_c);
        out.emplace_back(a.generic_string(), b.generic_string(), c.generic_string());
    }

    return out;
}

std::string ResourceLocator::ResolveFirstExisting(const std::string &relative_path,
                                                  int max_depth) {
    if (relative_path.empty()) {
        return {};
    }

    std::error_code ec;
    for (const auto &root : BuildSearchRoots(max_depth)) {
        const auto candidate = NormalizePath(root / relative_path);
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate.generic_string();
        }
    }
    return {};
}

}  // namespace desktoper2D
