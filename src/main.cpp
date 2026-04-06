#include <cpptrace/from_current.hpp>

#include "cli/cmd_height_map.h"
#include "cli/command_manager.h"
#include <CLI/CLI.hpp>
#include <csignal>
#include <cstdlib>


void crashHandler(int signal)
{
    cpptrace::generate_trace().print();
    std::exit(signal);
}

int main(int argc, char **argv)
{
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGSEGV, crashHandler);

    CPPTRACE_TRY
    {
        CLI::App app;

        app.description("HeightMap Generator");
        app.name("hmapgen.exe");
        app.allow_extras(true);

        CommandManager manager;

        manager.add<CmdHeightMap>();

        manager.registerAll(app);

        CLI11_PARSE(app, argc, argv);
        return 0;
    }
    CPPTRACE_CATCH(const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        cpptrace::from_current_exception().print();
    }
}