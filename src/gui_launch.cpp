#include "gui_launch.hpp"

#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

struct _XDisplay; // Opaque Xlib handle.

namespace
{

bool displayAvailable()
{
    const char *name = std::getenv("DISPLAY");
    if (!name || !*name)
        return false;
    // Keep Xlib and LibLapin out of the launcher's load-time dependencies.
    auto *library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return false;
    const auto open = reinterpret_cast<_XDisplay *(*)(const char *)>(dlsym(library, "XOpenDisplay"));
    const auto close = reinterpret_cast<int (*)(_XDisplay *)>(dlsym(library, "XCloseDisplay"));
    auto *connection = open && close ? open(name) : nullptr;
    const bool available = connection != nullptr;
    if (available)
        close(connection);
    dlclose(library);
    return available;
}

} // namespace

std::optional<int> launchGui(const std::filesystem::path &executable, char **arguments)
{
    if (access(executable.c_str(), X_OK) != 0 || !displayAvailable())
        return std::nullopt;
    int ready[2];
    if (pipe(ready) != 0)
        return std::nullopt;

    if (ready[1] < 3)
    {
        const int fd = fcntl(ready[1], F_DUPFD, 3);
        close(ready[1]);
        if (fd < 0)
        {
            close(ready[0]);
            return std::nullopt;
        }
        ready[1] = fd;
    }

    const auto descriptor = std::to_string(ready[1]);
    const pid_t child = fork();
    if (child == 0)
    {
        close(ready[0]);
        if (setenv("BSTEP_GUI_READY_FD", descriptor.c_str(), 1) == 0)
            execv(executable.c_str(), arguments);
        _exit(127);
    }
    close(ready[1]);
    if (child < 0)
    {
        close(ready[0]);
        return std::nullopt;
    }

    // EOF before the signal means that even a failure in library initialization
    // can fall back to the TUI. After startup, return the graphical program's status.
    char opened = 0;
    ssize_t result;
    do { result = read(ready[0], &opened, 1); } while (result < 0 && errno == EINTR);
    close(ready[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (result != 1 || opened != '1')
        return std::nullopt;
    if (waited < 0)
        return 1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
