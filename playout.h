#ifndef PLAYOUT_H_
#define PLAYOUT_H_

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

// The declaration of the Playout class and its helpers.

#include <map>
#include <vector>

#include "havannah.h"
#include "options.h"
#include "patterns.h"
#include "rng.h"

namespace lajkonik {

class TwoMoves {
 public:
  TwoMoves() {}
  explicit TwoMoves(Cell fst) : first_(fst), second_(kZerothCell) {}
  TwoMoves(Cell fst, Cell snd) : first_(fst), second_(snd) {}
  ~TwoMoves() {}

  bool has_one_move() const { return (second_ == kZerothCell); }
  Cell first() const { return first_; }
  Cell second() const { return second_; }
  bool operator==(const TwoMoves& other) const {
    return first_ == other.first_;
  }
  bool operator!=(const TwoMoves& other) const {
    return first_ != other.first_;
  }
  bool operator<(const TwoMoves& other) const {
    return first_ < other.first_;
  }

 private:
  Cell first_;
  Cell second_;
};

class Playout {
 public:
  Playout(PlayoutOptions* playout_options,
          const Patterns* playout_patterns,
          unsigned seed);
  ~Playout() {}

  void PrepareForPlayingFromPosition(const Position* position);

  int Play(Player player, Cell last_move,
           int rave[2][kNumMovesOnBoard], int* num_moves);

  PlayoutOptions* options() const { return options_; }
  Rng* rng() { return &rng_; }

 private:
  int ReplaceMovesInRingFrames(Player player, int offset);
  void ReplaceMove(int i, Cell cell);
  void LookForMate(
      Player player, Cell cell, ChainNum current_chain, unsigned mask[]);
  int ForceMateInOne(int i, int index, const TwoMoves mating_moves[2]);
  int ForceMateInTwo(int i, const TwoMoves mating_moves[2]);
  int HavannahMate(Player player, int i);

  PlayoutOptions* options_;
  const Patterns* patterns_;

  Rng rng_;
  const Position* position_;
  Position mutable_position_;
  std::vector<Cell> free_cells_;
  std::vector<Cell> playout_moves_;
  int reverse_playout_moves_[kNumCellsWithSentinels];
  std::vector<Player> playout_players_;
  std::vector<TwoMoves> mate_threats_;
  std::vector<Cell> connecting_cells_;
  std::vector<Cell> neighbors_;
  std::vector<Cell> further_neighbors_;
  std::vector<Cell> other_two_bridge_cells_;
  std::vector<Cell> tmp0_;
  std::vector<Cell> tmp1_;
  std::vector<Cell> mates_in_one_move_;
  std::vector<int> mates_in_two_moves_indices_;
  TwoMoves winning_moves_[kNumMovesOnBoard][2];
  int winning_move_count_;
  int canned_moves_;
  int chance_of_forced_connection_;
  int chance_of_connection_defense_;
  std::map<ChainNum, std::vector<Cell> > ring_closing_moves_;

  Playout(const Playout&);
  void operator=(const Playout&);
};

}  // namespace lajkonik

#endif  // PLAYOUT_H_
