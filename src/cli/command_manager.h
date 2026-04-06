#pragma once

#include "CLI/CLI.hpp"
#include "core/app_context.h"
#include <memory>
#include <vector>

class BaseCommand
{
public:
    virtual ~BaseCommand() = default;
    virtual CLI::App *registerCommand(CLI::App &app) = 0;
    virtual void executeCommand(AppContext &appContext) = 0;
};

class CommandManager
{
    AppContext appContext;
    std::vector<std::unique_ptr<BaseCommand>> commands;

public:
    void registerAll(CLI::App &app)
    {
        for (auto &cmd : commands)
        {
            auto *subCommand = cmd->registerCommand(app);
            auto *cmdPtr = cmd.get();

            subCommand->callback([this, cmdPtr]()
            {
                cmdPtr->executeCommand(appContext);
            });
        }
    }

    template <typename T>
    void add()
    {
        commands.push_back(std::make_unique<T>());
    }
};