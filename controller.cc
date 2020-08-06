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

// Definition of the game controller class.

#include "controller.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>

#include "mcts.h"
#include "wfhashmap.h"

namespace lajkonik {

static const char kLogFileName[] = "lajkonik.log";

Controller::Controller(const ControllerOptions& options,
                       const std::vector<MctsEngine*>& engines)
  : options_(options),
    engines_(engines),
    player_(kWhite),
    has_swapped_(false),
    forced_result_(0),
    evaluation_(0.0f),
    highest_win_ratio_(0.0f) {
  assert(!engines.empty());
  threads_.resize(engines.size());
  current_position_.InitToStartPosition();
  if (pthread_mutex_init(&thread_num_mutex_, NULL) != 0) {
    fprintf(stderr, "Cannot create a mutex\n");
    exit(EXIT_FAILURE);
  }
}

Controller::~Controller() {
  pthread_mutex_destroy(&thread_num_mutex_);
}

void Controller::ClearTranspositionTable() {
  engines_[0]->ClearTranspositionTable();
}

std::string Controller::SuggestMove(Player pl, int thinking_time) {
  if (options_.use_swap && !has_swapped_ &&
      current_position_.MoveCount() == 1)
    return "swap";
  terminate_ = false;
  player_ = pl;
  fprintf(stderr, "Creating thread");
  thread_num_ = 0;
  for (int i = 0, size = threads_.size(); i < size; ++i) {
    assert(engines_[i] != NULL);
    engines_[i]->mark_as_not_running();
    if (pthread_create(&threads_[i], NULL,
                       Controller::StartEngineForPthreads, this) != 0) {
      fprintf(stderr, "Cannot start background thread no. %d.\n", i);
      exit(EXIT_FAILURE);
    }
  }
  MoveInfo move_1;
  MoveInfo move_2;
  if (thinking_time == 0)
    thinking_time = options_.seconds_per_move;
  for (int sec = 1; sec <= thinking_time; ++sec) {
    sleep(1);
    if (options_.print_debug_info)
      engines_[0]->PrintDebugInfo(sec);
    if (!engines_[0]->is_running())
      continue;
    engines_[0]->GetTwoBestMoves(&move_1, &move_2);
    if (move_1.move == kInvalidMove)
      continue;
    if (ResultIsForced(move_1.num_simulations) ||
        (ResultIsForced(move_2.num_simulations) &&
         move_1.win_ratio > options_.sole_nonlosing_move_win_ratio_threshold))
      break;
    if (options_.use_human_like_time_control &&
        (move_1.num_simulations * move_1.win_ratio * sec >
         move_2.num_simulations * options_.seconds_per_move))
      break;
  }
  terminate_ = true;
  void* ignored;
  for (int i = 0, size = threads_.size(); i < size; ++i) {
    if (pthread_join(threads_[i], &ignored) != 0) {
      fprintf(stderr, "Cannot join background thread no %d.\n", i);
      exit(EXIT_FAILURE);
    }
  }
  evaluation_ = move_1.win_ratio;
  if (move_1.win_ratio > highest_win_ratio_) {
    highest_win_ratio_ = move_1.win_ratio;
    highest_win_move_ = current_position_.MoveCount();
  }
  if (ResultIsForced(move_1.num_simulations)) {
    forced_result_ = move_1.num_simulations;
  }
  if (move_1.move == kInvalidMove)
    return "pass";
  else
    return ToString(current_position_.MoveIndexToCell(move_1.move));
}

void Controller::Reset() {
  while (Undo()) {
    continue;
  }
  has_swapped_ = false;
  highest_win_ratio_ = 0.0f;
}

bool Controller::Undo() {
  return current_position_.UndoPermanentMove();
}

void* Controller::StartEngineForPthreads(void* obj) {
  Controller* controller = reinterpret_cast<Controller*>(obj);

  pthread_mutex_lock(&controller->thread_num_mutex_);
  const int thread_num = controller->thread_num_;
  AtomicIncrement(&controller->thread_num_, 1);
  pthread_mutex_unlock(&controller->thread_num_mutex_);

  assert(controller->engines_[thread_num] != NULL);
  fprintf(stderr, " %d", thread_num + 1);
  controller->engines_[thread_num]->SearchForMove(
      controller->player_,
      controller->current_position_,
      &controller->terminate_);
  return NULL;
}

bool Controller::MakeMove(
    Player pl, const std::string& move_string, int* result) {
  if (move_string == "pass") {
    *result = kNoneWon;
    return true;
  } else if (move_string == "swap") {
    current_position_.SwapPlayers();
    has_swapped_ = true;
    *result = kNoneWon;
    return true;
  }
  const Cell cell = FromString(move_string);
  if (cell == kZerothCell)
    return false;
  if (!current_position_.CellIsEmpty(cell))
    return false;
  const int move_result = current_position_.MakePermanentMove(pl, cell);
  if (move_result != 0) {
    fprintf(stderr, (pl == kWhite) ? "White won\n" : "Black won\n");
    *result = (pl == kWhite) ? kWhiteWon : kBlackWon;
    return true;
  }
  if (options_.end_games_quickly) {
    if (VictoryIsForced(forced_result_)) {
      *result = (pl == kWhite) ? kWhiteWon : kBlackWon;
      return true;
    } else if (DrawIsForced(forced_result_)) {
      *result = kDraw;
      return true;
    } else if (DefeatIsForced(forced_result_)) {
      *result = (pl == kBlack) ? kWhiteWon : kBlackWon;
      return true;
    }
  }
  assert(move_result == 0);
  *result = kNoneWon;
  return true;
}

std::string Controller::GetBoardString() const {
  return current_position_.MakeString(current_position_.MoveNPliesAgo(0));
}

bool Controller::DumpGameTree(
    int depth, const std::string& filename, std::string* error) const {
  return engines_[0]->DumpGameTree(depth, filename, error);
}

std::string Controller::GetBoard() const {
  std::string result;
  for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
    for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
      if (!LiesOnBoard(x, y))
        continue;
      const Cell cell = XYToCell(x, y);
      result += current_position_.GetCell(cell)["0wb"];
      result += ' ';
    }
  }
  return result;
}
    
float Controller::GetEvaluation() const {
  return evaluation_;
}

void Controller::GetPositions(
    int lower, int upper, std::vector<std::vector<Cell> >* move_list) const {
  engines_[0]->GetPositions(lower, upper, move_list);
}

void Controller::GetStatus(
    std::string* first_status, std::string* second_status) const {
  engines_[0]->GetStatus(current_position_, first_status, second_status);
}

void Controller::GetSgf(int threshold, std::string* sgf) const {
  engines_[0]->GetSgf(threshold, sgf);
}

void Controller::LogDebugInfo(Player pl) {
  if (highest_win_ratio_ < options_.win_ratio_threshold)
    return;
  std::string options_string = options_.ToString() +
                               engines_[0]->mcts_options()->ToString() +
                               engines_[0]->playout_options()->ToString();
  FILE* log = fopen(kLogFileName, "at");
  if (log == NULL)
    return;
  fprintf(log, "%s", options_string.c_str());
  for (int i = current_position_.MoveCount() - 1; i >= 0; --i) {
    fprintf(log, "%s ", ToString(current_position_.MoveNPliesAgo(i)).c_str());
  }
  Cell last_move = kZerothCell;
  while (current_position_.MoveCount() > highest_win_move_) {
    last_move = current_position_.MoveNPliesAgo(0);
    current_position_.UndoPermanentMove();
  }
  fprintf(log, "\n%c believed %.2f%% in victory before move %d (%s)\n",
          pl["xo"], 100.0f * highest_win_ratio_, highest_win_move_,
          ToString(last_move).c_str());
  fprintf(log, "%s\n", current_position_.MakeString().c_str());
  fclose(log);
}

int Controller::node_count() const {
  return engines_[0]->node_count();
}

MctsOptions* Controller::mcts_options() {
  return engines_[0]->mcts_options();
}

PlayoutOptions* Controller::playout_options() {
  return engines_[0]->playout_options();
}

}  // namespace lajkonik
