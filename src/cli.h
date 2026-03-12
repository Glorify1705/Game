#pragma once
#ifndef _GAME_CLI_H
#define _GAME_CLI_H

namespace G {

int CmdInit(int argc, const char* argv[]);
int CmdRun(int argc, const char* argv[]);
int CmdRunPackaged(int argc, const char* argv[]);
int CmdPackage(int argc, const char* argv[]);
int CmdStubs(int argc, const char* argv[]);
int CmdVersion();
int CmdHelp(const char* subcommand);
bool PackagedGameExists(const char* argv0);

}  // namespace G

#endif  // _GAME_CLI_H
