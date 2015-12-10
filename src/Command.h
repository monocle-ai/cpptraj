#ifndef INC_COMMAND_H
#define INC_COMMAND_H
#include "CmdList.h"
#include "CpptrajState.h"
class Command {
  public:
    static void Init();
    static void Free();
    /// List commands of given type, or all if type is NONE
    static void ListCommands(DispatchObject::Otype);
    /// Add a command to the list of commands
    static void AddCmd(DispatchObject*, Cmd::DestType, int, ...);
    /// \return command corresponding to first argument in ArgList.
    static Cmd const& SearchToken(ArgList&);
    /// \return command of given type corresponding to given command key. 
    static Cmd const& SearchTokenType(DispatchObject::Otype, const char*);
    /// Execute command, modifies given CpptrajState
    static CpptrajState::RetType Dispatch(CpptrajState&, std::string const&);
    /// Read input commands from given file, modifies given CpptrajState.
    static CpptrajState::RetType ProcessInput(CpptrajState&, std::string const&);
  private:
    static void WarnDeprecated(const char*, Cmd const&);
    static void ListCommandsForType(DispatchObject::Otype);

    static CmdList commands_; ///< Master list of commands.
    static const Cmd EMPTY_;  ///< Empty command.
};
#endif
