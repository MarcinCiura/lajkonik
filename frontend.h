#ifndef FRONTEND_H_
#define FRONTEND_H_
// Copyright (c) 2012 Marcin Ciura, Piotr Wieczorek
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Declaration of the interpreter of a subset of Go Text Protocol.

#include <stdarg.h>
#include <vector>
#include "controller.h"
#include "havannah.h"

namespace lajkonik {

// A parser for the subset of GTP v2 applicable to Havannah.
class Frontend {
 public:
  // Does not take ownership of the pointers.
  Frontend(
      Controller* controller,
      Player* player,
      int* result,
      volatile bool* is_thinking);
  ~Frontend();
  std::vector<char*> HandleCommand(char* command);
  static char* CommandGenerator(const char* text, int state);
  Controller* get_controller() const { return controller_; }
  bool get_is_thinking() const { return *is_thinking_; }
  int get_result() const { return *result_; }
  bool command_succeeded() const { return command_succeeded_; }

 private:
  static const char kSuccess = '=';
  static const char kFailure = '?';

  struct Command {
    const char* name;
    void (Frontend::*method)(const std::vector<char*>& args);
  };

  virtual void VPrintf(const char* format, va_list ap) const = 0;
  virtual void Flush() const = 0;

  void StartAnswer(char indicator);
  void Answer(char indicator, const char* format, ...);
  void Printf(const char* format, ...);

  bool StrToFloat(const char* str, float* v);
  bool StrToInt(const char* str, int* v);
  bool StrToBool(const char* str, bool* v);

  void Boardsize(const std::vector<char*>& args);
  void ClearBoard(const std::vector<char*>& args);
  void CountNodes(const std::vector<char*>& args);
  void DumpTree(const std::vector<char*>& args);
  void Genmove(const std::vector<char*>& args);
  void GetEval(const std::vector<char*>& args);
  void GetPositions(const std::vector<char*>& args);
  void GetSgf(const std::vector<char*>&args);
  void HavannahWinner(const std::vector<char*>& args);
  void KnownCommand(const std::vector<char*>& args);
  void Komi(const std::vector<char*>& args);
  void ListCommands(const std::vector<char*>& args);
  void ListOptions(const std::vector<char*>& args);
  void Name(const std::vector<char*>& args);
  void Play(const std::vector<char*>& args);
  void PlayGame(const std::vector<char*>& args);
  void ProtocolVersion(const std::vector<char*>& args);
  void SetOption(const std::vector<char*>& args);
  void Showboard(const std::vector<char*>& args);
  void ShowOption(const std::vector<char*>& args);
  void Quit(const std::vector<char*>& args);
  void Undo(const std::vector<char*>& args);
  void Version(const std::vector<char*>& args);
  
  bool Tokenize(
      char* input, char** command, std::vector<char*>* args) const;
  //
  static const Command kCommands[];
  //
  std::vector<std::pair<const char*, float*> > float_options_;
  std::vector<std::pair<const char*, int*> > int_options_;
  std::vector<std::pair<const char*, bool*> > bool_options_;
  //
  Controller* controller_;
  //
  Player* player_;
  //
  int* result_;
  //
  volatile bool* is_thinking_;
  //
  int command_id_;
  //
  bool command_succeeded_;

  Frontend(const Frontend&);
  void operator=(const Frontend&);
};

}  // namespace lajkonik

#endif  // FRONTEND_H_