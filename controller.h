#ifndef CONTROLLER_H_
#define CONTROLLER_H_

// Copyright (c) 2010-2012 Marcin Ciura, Piotr Wieczorek
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

// Declaration of the game controller class.

#include <pthread.h>
#include <string>
#include <vector>

#include "havannah.h"
#include "options.h"

namespace lajkonik {

class MctsEngine;
struct MctsOptions;
struct PlayoutOptions;

// TODO(mciura)
enum {
  kNoneWon,
  kWhiteWon,
  kDraw,
  kBlackWon
};

class Controller {
 public:
  // Does not take the ownership of engine.
  Controller(const ControllerOptions& options,
             const std::vector<MctsEngine*>& engines);
  ~Controller();

  void ClearTranspositionTable();
  std::string SuggestMove(Player player, int thinking_time);
  bool MakeMove(Player player, const std::string& move_string, int* result);
  void Reset();
  bool Undo();
  std::string GetBoardString() const;
  bool DumpGameTree(
      int depth, const std::string& filename, std::string* error) const;
  std::string GetBoard() const;
  float GetEvaluation() const;
  void GetPositions(
      int lower, int upper, std::vector<std::vector<Cell> >* move_list) const;
  void GetStatus(std::string* first_status, std::string* second_status) const;
  void GetSgf(int threshold, std::string* sgf) const;
  void LogDebugInfo(Player player);

  int node_count() const;
  const Position& position() const { return current_position_; }
  Player player() const { return player_; }
  ControllerOptions* controller_options() { return &options_; }
  MctsOptions* mcts_options();
  PlayoutOptions* playout_options();

 private:
  static void* StartEngineForPthreads(void* obj);

  Position current_position_;
  ControllerOptions options_;

  const std::vector<MctsEngine*>& engines_;
  std::vector<pthread_t> threads_;
  int thread_num_;
  pthread_mutex_t thread_num_mutex_;

  Player player_;
  int suggested_move_;
  volatile bool terminate_;
  bool has_swapped_;
  int forced_result_;
  float evaluation_;
  float highest_win_ratio_;
  int highest_win_move_;

  Controller(const Controller&);
  void operator=(const Controller&);
};

}  // namespace lajkonik

#endif  // CONTROLLER_H_
