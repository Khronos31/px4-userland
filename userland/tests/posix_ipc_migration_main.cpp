// SPDX-License-Identifier: GPL-2.0-only
#include <cstdio>

bool run_posix_ipc_tests();

int main()
{
    const bool passed = run_posix_ipc_tests();
    std::printf("%s posix_ipc_tests\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
