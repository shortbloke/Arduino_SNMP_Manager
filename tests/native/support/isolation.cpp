#include "isolation.h"
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>

TestResult runIsolated(const std::function<void()> &run)
{
    FILE *diagnostics = tmpfile();
    if (!diagnostics)
        return {false, "Could not create test diagnostics file"};
    std::cout.flush();
    std::cerr.flush();
    pid_t child = fork();
    if (child == 0)
    {
        if (dup2(fileno(diagnostics), STDERR_FILENO) < 0)
            _exit(2);
        alarm(5);
        try
        {
            run();
            std::cout.flush();
            _exit(0);
        }
        catch (const std::exception &error)
        {
            std::cerr << error.what() << std::endl;
            _exit(1);
        }
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    rewind(diagnostics);
    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), diagnostics))
        output += buffer;
    fclose(diagnostics);
    if (waited != child || child < 0)
        return {false, "Could not execute isolated test\n" + output};
    if (WIFSIGNALED(status))
        output += "signal " + std::to_string(WTERMSIG(status)) + "\n";
    return {WIFEXITED(status) && WEXITSTATUS(status) == 0, output};
}
