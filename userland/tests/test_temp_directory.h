// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_TEST_TEMP_DIRECTORY_H
#define PX4_USERLAND_TEST_TEMP_DIRECTORY_H

#include <cstdlib>
#include <string>
#include <string_view>

namespace px4::userland::test {

inline std::string temporary_directory_template(std::string_view prefix)
{
    const char* configured_root = std::getenv("TMPDIR");
    std::string root = configured_root != nullptr && configured_root[0] == '/'
                           ? configured_root
                           : "/tmp";

    while (root.size() > 1U && root.back() == '/') {
        root.pop_back();
    }

    std::string pattern = root;
    if (root.back() != '/') {
        pattern.push_back('/');
    }
    pattern.append(prefix);
    pattern.append("XXXXXX");
    return pattern;
}

} // namespace px4::userland::test

#endif
