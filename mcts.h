#ifndef MCTS_H_
#define MCTS_H_

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

// The declaration of the MctsEngine class.

#include <limits.h>
#include <math.h>
#include <string>
#include <vector>

#include "havannah.h"
#include "options.h"

namespace lajkonik {

class MctsNode;
class Playout;
class TranspositionTable;

const int kBoardFilledDraw = 0x8000 - INT_MAX;

// TODO(mciura)
static inline bool VictoryIsForced(int result) {
  return (result >= INT_MAX - 0x8000);
}
static inline bool DrawIsForced(int result) {
  return (result == kBoardFilledDraw);
}
static inline bool DefeatOrDrawIsForced(int result) {
  return (result <= kBoardFilledDraw);
}
static inline bool DefeatIsForced(int result) {
  return (result < kBoardFilledDraw);
}
static inline bool ResultIsForced(int result) {
  return VictoryIsForced(result) || DefeatOrDrawIsForced(result);
}

enum {
  kHoeffding,
  kHoeffdingSlow,
  kGelly,
  kTeytaud,
  kSilver,
  kSilverWithProgressiveBias,
  kSilverUnsimplified,
  kNijssenWinands,
  kNumStrategies,
};

struct MoveInfo {
  MoveIndex move;
  int num_simulations;
  float win_ratio;
};

class Statistics {
 public:
  Statistics() { Init(); }
  ~Statistics() {}

  void Init() {
    n_ = 0;
    m_ = 0.0;
    v_ = 0.0;
  }

  // Adds the x sample to the statistics.
  void Add(double x) {
    ++n_;
    double d = x - m_;
    m_ += d / n_;
    v_ += d * (x - m_);
  }
  // Returns the mean of samples.
  double Mean() const { return m_; }
  // Returns the standard deviation of samples.
  double StdDev() const { return sqrt(v_ / (n_ - 1)); }
  // Returns the number of samples.
  int N() const { return n_; }

 private:
  // The number of samples.
  int n_;
  // The mean.
  double m_;
  // The variance.
  double v_;
};

// TODO(mciura)
class MctsEngine {
 public:
  // Does not take ownership of mcts_options or playout.
  MctsEngine(MctsOptions* mcts_options, Playout* playout);
  ~MctsEngine();

  //
  void ClearTranspositionTable();
  //
  void SearchForMove(Player player,
                     const Position& start_position,
                     volatile bool* terminate);
  //
  void GetTwoBestMoves(MoveInfo* move_1, MoveInfo* move_2) const;
  //
  void PrintDebugInfo(int secs);
  //
  bool DumpGameTree(
      int depth, const std::string& filename, std::string* error) const;
  //
  void GetPositions(
      int lower, int upper, std::vector<std::vector<Cell> >* cell_list) const;
  //
  void GetStatus(const Position& start_position,
                 std::string* first_status, std::string* second_status) const;
  //
  void GetSgf(int threshold, std::string* sgf) const;
  //
  void mark_as_not_running() { is_running_ = false; }
  //
  bool is_running() const { return is_running_; }
  //
  int node_count() const;
  // Getter for options_.
  MctsOptions* mcts_options() { return options_; }
  // Getter for playout options.
  PlayoutOptions* playout_options();

 private:
  //
  void UpdateRaveInTree(Hash position_hash,
                        Player player,
                        int rave_i,
                        int reward,
                        int num_simulations);
  // A helper for playout_->PlayOnce(). Translates its result
  // into +1, 0, or -1 from player's point of view.
  int GetPlayoutResult(Player player, Cell last_move, int empty_cell_count);
  //
  int Descend(Hash position_hash,
              MctsNode* node,
              Player player,
              Cell last_move,
              int empty_cell_count);
  // Updates the UCB reward and number of simulations for node
  // (== node_map_->FindNode(position_hash)) that corresponds to the given
  // position with the given player to move. Returns the UCB reward.
  int UpdateNodeAndGetReward(Hash position_hash,
                             MctsNode* node,
                             Player player,
                             Cell last_move,
                             int empty_cell_count);
  //
  void RecursiveGetSgf(
      Player player,
      Hash hash,
      Cell cell,
      int threshold,
      std::string* sgf) const;

  // Maps Zobrist hashes of positions to MctsNodes.
  TranspositionTable* transposition_table_;
  // TODO(mciura)
  Playout* playout_;
  //
  Position position_;
  //
  Memento memento_;
  // Options relevant to Monte-Carlo Tree Search.
  MctsOptions* options_;
  //
  int empty_cell_count_at_bottom_;
  //
  bool is_running_;
  //
  Player player_;
  // Moves made in the game tree.
  std::vector<Cell> moves_;
  //
  Statistics stats_[kNumMovesOnBoard + 1];
  //
  int rave_[2][kNumMovesOnBoard];

  MctsEngine(const MctsEngine&);
  void operator=(const MctsEngine&);
};

}  // namespace lajkonik

#endif  // MCTS_H_
