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

// Interpreter of the subset of Go Text Protocol v2 applicable to Havannah.

#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "define-playout-patterns.h"

namespace lajkonik {

const Frontend::Command Frontend::kCommands[] = {
  { "boardsize", &Frontend::Boardsize },
  { "clearboard", &Frontend::ClearBoard },
  { "countnodes", &Frontend::CountNodes },
  { "dumptree", &Frontend::DumpTree },
  { "genmove", &Frontend::Genmove },
  { "geteval", &Frontend::GetEval },
  { "getpositions", &Frontend::GetPositions },
  { "getsgf", &Frontend::GetSgf },
  { "havannahwinner", &Frontend::HavannahWinner },
  { "knowncommand", &Frontend::KnownCommand },
  { "komi", &Frontend::Komi },
  { "listcommands", &Frontend::ListCommands },
  { "listoptions", &Frontend::ListOptions },
  { "name", &Frontend::Name },
  { "play", &Frontend::Play },
  { "playgame", &Frontend::PlayGame },
  { "protocolversion", &Frontend::ProtocolVersion },
  { "setoption", &Frontend::SetOption },
  { "showboard", &Frontend::Showboard },
  { "showoption", &Frontend::ShowOption },
  { "quit", &Frontend::Quit },
  { "undo", &Frontend::Undo },
  { "version", &Frontend::Version },
  { NULL, NULL },
};

Frontend::Frontend(
    Controller* controller,
    Player* player,
    int* result,
    volatile bool* is_thinking)
    : controller_(controller),
      player_(player),
      result_(result),
      is_thinking_(is_thinking),
      command_id_(-1),
      command_succeeded_(false) {
  PlayoutOptions* playout_options = controller->playout_options();
  MctsOptions* mcts_options = controller->mcts_options();
  ControllerOptions* controller_options = controller->controller_options();

#define ADD_OPTION(v, o, n) (v).push_back(std::make_pair(#n, &(o)->n))
  ADD_OPTION(float_options_, playout_options, initial_chance_of_ring_notice);
  ADD_OPTION(float_options_, playout_options, final_chance_of_ring_notice);
  ADD_OPTION(float_options_,
             playout_options, chance_of_forced_connection_slope);
  ADD_OPTION(float_options_,
             playout_options, chance_of_forced_connection_intercept);
  ADD_OPTION(float_options_,
             playout_options, chance_of_connection_defense_slope);
  ADD_OPTION(float_options_,
             playout_options, chance_of_connection_defense_intercept);
  ADD_OPTION(float_options_, mcts_options, exploration_factor);
  ADD_OPTION(float_options_, mcts_options, rave_bias);
  ADD_OPTION(float_options_, mcts_options, first_play_urgency);
  ADD_OPTION(float_options_, mcts_options, tricky_epsilon);
  ADD_OPTION(float_options_, mcts_options, locality_bias);
  ADD_OPTION(float_options_, mcts_options, chain_size_bias_factor);
  ADD_OPTION(float_options_, controller_options,
             sole_nonlosing_move_win_ratio_threshold);
  ADD_OPTION(float_options_, controller_options, win_ratio_threshold);

  ADD_OPTION(int_options_, playout_options, retries_of_isolated_moves);
  ADD_OPTION(int_options_, mcts_options, expand_after_n_playouts);
  ADD_OPTION(int_options_, mcts_options, play_n_playouts_at_once);
  ADD_OPTION(int_options_, mcts_options, exploration_strategy);
  ADD_OPTION(int_options_, mcts_options, rave_update_depth);
  ADD_OPTION(int_options_, mcts_options, prior_num_simulations_base);
  ADD_OPTION(int_options_, mcts_options, prior_num_simulations_range);
  ADD_OPTION(int_options_, mcts_options, prior_reward_halfrange);
  ADD_OPTION(int_options_, mcts_options, neighborhood_size);
  ADD_OPTION(int_options_, controller_options, seconds_per_move);

  bool_options_.push_back(
      std::make_pair("use_lg_coordinates", &g_use_lg_coordinates));
  ADD_OPTION(bool_options_, playout_options, use_havannah_mate);
  ADD_OPTION(bool_options_, playout_options, use_havannah_antimate);
  ADD_OPTION(bool_options_, playout_options, use_ring_detection);
  ADD_OPTION(bool_options_, mcts_options, use_rave_randomization);
  ADD_OPTION(bool_options_, mcts_options, use_mate_in_tree);
  ADD_OPTION(bool_options_, mcts_options, use_antimate_in_tree);
  ADD_OPTION(bool_options_, mcts_options, use_deeper_mate_in_tree);
  ADD_OPTION(bool_options_, mcts_options, use_virtual_loss);
  ADD_OPTION(bool_options_, mcts_options, use_solver);
  ADD_OPTION(bool_options_, controller_options, end_games_quickly);
  ADD_OPTION(bool_options_, controller_options, print_debug_info);
  ADD_OPTION(bool_options_, controller_options, use_human_like_time_control);
  ADD_OPTION(bool_options_, controller_options, use_swap);
#undef ADD_OPTION
}

Frontend::~Frontend() {}

void Frontend::StartAnswer(char indicator) {
  if (command_id_ >= 0)
    Printf("%c%d ", indicator, command_id_);
  else
    Printf("%c ", indicator);
  command_succeeded_ = (indicator == kSuccess);
}

void Frontend::Answer(char indicator, const char* format, ...) {
  StartAnswer(indicator);
  va_list ap;
  va_start(ap, format);
  VPrintf(format, ap);
  va_end(ap);
  Printf("\n\n");
  Flush();
}

void Frontend::Printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  VPrintf(format, ap);
  va_end(ap);
}

bool Frontend::StrToFloat(const char* str, float* v) {
  char* endptr;
  *v = strtod(str, &endptr);
  if (*endptr != '\0') {
    Answer(kFailure, "invalid float %s", str);
    return false;
  } else {
    return true;
  }
}

bool Frontend::StrToInt(const char* str, int* v) {
  char* endptr;
  *v = strtol(str, &endptr, 10);
  if (*endptr != '\0') {
    Answer(kFailure, "invalid integer %s", str);
    return false;
  } else {
    return true;
  }
}

bool Frontend::StrToBool(const char* str, bool* v) {
  if (strcmp(str, "true") == 0) {
    *v = true;
    return true;
  } else if (strcmp(str, "false") == 0) {
    *v = false;
    return true;
  } else {
    Answer(kFailure, "invalid bool %s", str);
    return false;
  }
}

namespace {

bool GetColor(const char* s, Player* color) {
  if (strcmp(s, "w") == 0 || strcmp(s, "white") == 0)
    *color = kWhite;
  else if (strcmp(s, "b") == 0 || strcmp(s, "black") == 0)
    *color = kBlack;
  else
    return false;
  return true;
}

}  // namespace

void Frontend::Boardsize(const std::vector<char*>& args) {
  int size;
  if (args.size() != 1)
    Answer(kFailure, "expected one argument to boardsize");
  else if (StrToInt(args[0], &size) && size == SIDE_LENGTH)
    Answer(kSuccess, "");
  else
    Answer(kFailure, "unacceptable size %s", args[0]);
}

void Frontend::ClearBoard(const std::vector<char*>& /*args*/) {
  controller_->Reset();
  *result_ = kNoneWon;
  Answer(kSuccess, "");
}

void Frontend::CountNodes(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "%d", controller_->node_count());
}

void Frontend::DumpTree(const std::vector<char*>& args) {
  int depth;
  if (args.size() != 2) {
    Answer(kFailure, "expected two arguments to dump_tree");
  } else if (StrToInt(args[0], &depth)) {
    std::string error;
    bool success = controller_->DumpGameTree(depth, args[1], &error);
    if (success)
      Answer(kSuccess, "");
    else
      Answer(kFailure, "%s", error.c_str());
  }
}

void Frontend::Genmove(const std::vector<char*>& args) {
  Player player = *player_;
  int thinking_time_index = 0;
  if (!args.empty() && GetColor(args[0], &player))
    thinking_time_index = 1;
  const int last_arg_index = args.size() - 1;
  int thinking_time;
  if (thinking_time_index == last_arg_index) {
    if (!StrToInt(args[thinking_time_index], &thinking_time)) {
      Answer(kFailure, "invalid arguments to genmove");
      return;
    }
  } else if (last_arg_index > thinking_time_index) {
    Answer(kFailure, "too many arguments to genmove");
    return;
  } else {
    thinking_time = 0;
  }

  if (*result_ == kNoneWon) {
    *is_thinking_ = true;
    if (!controller_->controller_options()->clear_tt_after_move)
      controller_->ClearTranspositionTable();
    const std::string move = controller_->SuggestMove(player, thinking_time);
    if (!controller_->MakeMove(player, move, result_)) {
      fprintf(stderr, "Unexpected move %s", move.c_str());
      exit(EXIT_FAILURE);
    }
    Answer(kSuccess, "%s", move.c_str());
    if (controller_->controller_options()->clear_tt_after_move)
      controller_->ClearTranspositionTable();
    *player_ = Opponent(player);
    *is_thinking_ = false;
  } else {
    Answer(kSuccess, "none");
  }
}
    
void Frontend::GetEval(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "%.2f", 100.0f * controller_->GetEvaluation());
}

void Frontend::GetPositions(const std::vector<char*>& args) {
  int lower;
  int upper;
  if (args.size() != 2) {
    Answer(kFailure, "expected two arguments to get_positions");
  } else if (StrToInt(args[0], &lower) && StrToInt(args[1], &upper)) {
    StartAnswer(kSuccess);
    Printf("\n");
    std::vector<std::vector<Cell> > move_list;
    controller_->GetPositions(lower, upper, &move_list);
    for (int i = 0, isize = move_list.size(); i < isize; ++i) {
      const Position& position = controller_->position();
      for (int j = position.MoveCount() - 1; j >= 0; --j) {
        Printf("%s ", ToString(position.MoveNPliesAgo(j)).c_str());
      }
      for (int j = 0, jsize = move_list[i].size(); j < jsize; ++j) {
        Printf((j < jsize) ? "%s " : "%s", ToString(move_list[i][j]).c_str());
      }
      Printf("\n");
    }
  } else {
    Answer(kFailure, "unexpected arguments %s %s", args[0], args[1]);
  }
}

void Frontend::GetSgf(const std::vector<char*>& args) {
  int threshold;
  if (args.size() != 1) {
    Answer(kFailure, "expected one argument to get_sfg");
  } else if (StrToInt(args[0], &threshold)) {
    std::string sgf;
    controller_->GetSgf(threshold, &sgf);
    Answer(kSuccess, sgf.c_str());
  } else {
    Answer(kFailure, "unexpected argument %s", args[0]);
  }
}

void Frontend::HavannahWinner(const std::vector<char*>& /*args*/) {
  static const char* kWinner[4] = { "none", "white", "draw", "black" };
  assert(*result_ >= 0);
  assert(*result_ < 4);
  Answer(kSuccess, kWinner[*result_]);
}

void Frontend::KnownCommand(const std::vector<char*>& args) {
  if (args.size() >= 1) {
    for (int i = 0; kCommands[i].name != NULL; ++i) {
      if (strcmp(args[0], kCommands[i].name) == 0) {
        Answer(kSuccess, "true");
        return;
      }
    }
  }
  Answer(kSuccess, "false");
}

void Frontend::Komi(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "");
}

void Frontend::ListCommands(const std::vector<char*>& /*args*/) {
  StartAnswer(kSuccess);
  for (int i = 0; kCommands[i].name != NULL; ++i) {
    Printf("%s\n", kCommands[i].name);
  }
  Printf("\n");
}

void Frontend::ListOptions(const std::vector<char*>& /*args*/) {
  StartAnswer(kSuccess);
  Printf("\n");
  for (int i = 0, size = float_options_.size(); i < size; ++i) {
    Printf("%s = %f\n", float_options_[i].first, *float_options_[i].second);
  }
  for (int i = 0, size = int_options_.size(); i < size; ++i) {
    Printf("%s = %d\n", int_options_[i].first, *int_options_[i].second);
  }
  for (int i = 0, size = bool_options_.size(); i < size; ++i) {
    Printf("%s = %s\n", bool_options_[i].first,
           *bool_options_[i].second ? "true" : "false");
  }
  for (int i = 0; kPlayoutPatterns[i].neighbors != NULL; ++i) {
    Printf("%s %s\n", kPlayoutPatterns[i].neighbors, kPlayoutPatterns[i].mask);
  }
  Printf("\n");
}

void Frontend::Name(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "Lajkonik");
}

void Frontend::Play(const std::vector<char*>& args) {
  Player player;
  if (args.size() != 2) {
    Answer(kFailure, "expected two arguments to play");
  } else if (!GetColor(args[0], &player)) {
    Answer(kFailure, "invalid color %s", args[0]);
  } else {
    if (controller_->MakeMove(player, args[1], result_)) {
      Answer(kSuccess, "");
      *player_ = Opponent(player);
    } else {
      Answer(kFailure, "invalid move %s", args[1]);
    }
  }
}

void Frontend::PlayGame(const std::vector<char*>& args) {
  for (int i = 0, size = args.size(); i < size; ++i) {
    int unused;
    if (!controller_->MakeMove(*player_, args[i], &unused)) {
      Answer(kFailure, "invalid move %s", args[i]);
      for (/**/; i > 0; --i) {
        controller_->Undo();
      }
      return;
    }
    *player_ = Opponent(*player_);
  }
  Answer(kSuccess, "");
}

void Frontend::ProtocolVersion(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "2");
}

void Frontend::Quit(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, "");
  exit(EXIT_SUCCESS);
}

void Frontend::SetOption(const std::vector<char*>& args) {
  if (args.size() != 2) {
    Answer(kFailure, "expected two arguments to set_option");
  } else {
    for (int i = 0, size = float_options_.size(); i < size; ++i) {
      if (strcmp(args[0], float_options_[i].first) == 0) {
        if (StrToFloat(args[1], float_options_[i].second))
          Answer(kSuccess, "");
        return;
      }
    }
    for (int i = 0, size = int_options_.size(); i < size; ++i) {
      if (strcmp(args[0], int_options_[i].first) == 0) {
        if (StrToInt(args[1], int_options_[i].second))
          Answer(kSuccess, "");
        return;
      }
    }
    for (int i = 0, size = bool_options_.size(); i < size; ++i) {
      if (strcmp(args[0], bool_options_[i].first) == 0) {
        if (StrToBool(args[1], bool_options_[i].second))
          Answer(kSuccess, "");
        return;
      }
    }
    Answer(kFailure, "unknown option %s", args[0]);
  }
}

void Frontend::Showboard(const std::vector<char*>& /*args*/) {
  StartAnswer(kSuccess);
  Printf("\n%s\n", controller_->GetBoardString().c_str());
}

void Frontend::ShowOption(const std::vector<char*>& args) {
  if (args.size() != 1) {
    Answer(kFailure, "expected one argument to show_option");
  } else {
    for (int i = 0, size = float_options_.size(); i < size; ++i) {
      if (strcmp(args[0], float_options_[i].first) == 0) {
        Answer(kSuccess, "%s = %f", args[0], *float_options_[i].second);
        return;
      }
    }
    for (int i = 0, size = int_options_.size(); i < size; ++i) {
      if (strcmp(args[0], int_options_[i].first) == 0) {
        Answer(kSuccess, "%s = %d", args[0], *int_options_[i].second);
        return;
      }
    }
    for (int i = 0, size = bool_options_.size(); i < size; ++i) {
      if (strcmp(args[0], bool_options_[i].first) == 0) {
        Answer(kSuccess, "%s = %s", args[0],
               *bool_options_[i].second ? "true" : "false");
        return;
      }
    }
    Answer(kFailure, "unknown option %s", args[0]);
  }
}

void Frontend::Undo(const std::vector<char*>& /*args*/) {
  if (controller_->Undo()) {
    Answer(kSuccess, "");
  } else {
    Answer(kFailure, "cannot undo");
  }
}

void Frontend::Version(const std::vector<char*>& /*args*/) {
  Answer(kSuccess, __DATE__ ", " __TIME__);
}

bool Frontend::Tokenize(
    char* input, char** command, std::vector<char*>* args) const {
  char* saveptr;
  *command = strtok_r(input, " \t\n", &saveptr);
  if (*command == NULL)
    return false;
  {
  char* p = *command;
  char* q = *command;
  while (*p != '\0') {
    if (*p != '_') {
      *q = *p;
      ++q;
    }
    ++p;
  }
  *q = '\0';
  }
  while (true) {
    char* word = strtok_r(NULL, " \t\n", &saveptr);
    if (word == NULL)
      break;
    args->push_back(word);
  }
  return true;
}

std::vector<char*> Frontend::HandleCommand(char* input) {
  for (char* p = input; *p != '\0'; ++p) {
    *p = tolower(*p);
  }
  if (isdigit(*input))
    command_id_ = strtol(input, &input, 10);
  else
    command_id_ = -1;
  char* command;
  std::vector<char*> args;
  if (!Tokenize(input, &command, &args)) {
    Answer(kFailure, "invalid command");
    return args;
  }
  for (int i = 0; kCommands[i].name != NULL; ++i) {
    if (strcmp(command, kCommands[i].name) == 0) {
      (this->*kCommands[i].method)(args);
      Flush();
      return args;
    }
  }
  Answer(kFailure, "unknown command %s", command);
  return args;
}
  
char* Frontend::CommandGenerator(const char* text, int state) {
  static int list_index;
  static int len;
  const char* name;
  if (state == 0) {
    list_index = 0;
    len = strlen(text);
  }
  while ((name = kCommands[list_index].name) != NULL) {
    ++list_index;
    if (strncmp(name, text, len) == 0)
      return strdup(name);
  }
  return NULL;
}

}  // namespace lajkonik
