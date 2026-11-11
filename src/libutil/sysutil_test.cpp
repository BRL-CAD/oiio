// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/unittest.h>

#ifndef _WIN32
#    include <fcntl.h>
#    include <unistd.h>
#endif

using namespace OIIO;

int
main()
{
    OIIO_CHECK_ASSERT(Sysutil::terminal_columns() > 0);
    OIIO_CHECK_ASSERT(Sysutil::terminal_rows() > 0);
#ifndef _WIN32
    // Redirected input cannot provide terminal dimensions. Preserve defaults.
    const int saved_input = dup(STDIN_FILENO);
    OIIO_CHECK_ASSERT(saved_input >= 0);
    if (saved_input < 0)
        return unit_test_failures;
    const int redirected = open("/dev/null", O_RDONLY);
    OIIO_CHECK_ASSERT(redirected >= 0);
    if (redirected >= 0) {
        const int result = dup2(redirected, STDIN_FILENO);
        OIIO_CHECK_ASSERT(result >= 0);
        if (result >= 0) {
            OIIO_CHECK_EQUAL(Sysutil::terminal_columns(), 80);
            OIIO_CHECK_EQUAL(Sysutil::terminal_rows(), 24);
        }
        close(redirected);
    }
    OIIO_CHECK_ASSERT(dup2(saved_input, STDIN_FILENO) >= 0);
    close(saved_input);
#endif
    return unit_test_failures;
}
