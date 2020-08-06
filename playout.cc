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

// The definition of the Playout class.

#include "playout.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>

#ifdef DUMP_PLAYOUTS
  #define DUMP(statement) statement
#else
  #define DUMP(statement)
#endif

namespace lajkonik {

//-- Playout ----------------------------------------------------------
Playout::Playout(PlayoutOptions* playout_options,
                 const Patterns* patterns,
                 unsigned seed)
    : options_(playout_options),
      patterns_(patterns) {
  rng_.Init(seed);
}

void Playout::PrepareForPlayingFromPosition(const Position* position) {
  position->GetFreeCells(&free_cells_);
  position_ = position;
}

int Playout::Play(
    Player player,
    Cell last_move,
    int rave[2][kNumMovesOnBoard],
    int* num_moves) {
  mutable_position_.CopyFrom(*position_);
  playout_moves_.clear();
  for (int i = 0, size = free_cells_.size(); i < size; ++i) {
    const Cell cell = free_cells_[i];
    if (mutable_position_.CellIsEmpty(cell)) {
      playout_moves_.push_back(cell);
    }
  }
  rng_.Shuffle(playout_moves_.begin(), playout_moves_.end());
  for (int i = 0, size = playout_moves_.size(); i < size; ++i) {
    reverse_playout_moves_[playout_moves_[i]] = i;
  }
  canned_moves_ = std::max(
      ReplaceMovesInRingFrames(player, 0),
      ReplaceMovesInRingFrames(Opponent(player), 1));
  const int num_chains =
      mutable_position_.player_position(player).CountChains();
  chance_of_forced_connection_ =
      options_->chance_of_forced_connection_intercept +
      num_chains * options_->chance_of_forced_connection_slope;
  chance_of_connection_defense_ =
      options_->chance_of_connection_defense_intercept +
      num_chains * options_->chance_of_connection_defense_slope; 
 
  playout_players_.clear();
  int noli_me_tangere = -1;
  unsigned long long neighbors18 =
      mutable_position_.Get18Neighbors(player, last_move);
  int i;
  const int size = playout_moves_.size();
  DUMP(printf("-----------------------------------\n"));
  for (i = 0; i < size; ++i) {
    playout_players_.push_back(player);
    if (noli_me_tangere < 0) {
      DUMP(printf("Pattern at %s: %0llx\n",
                  ToString(last_move).c_str(),
                  neighbors18));
      const MoveSuggestion suggestion =
          patterns_->GetMoveSuggestion(neighbors18);
      if (suggestion.ChancesAreAuspicious(&rng_)) {
        const int index = suggestion.GetIndexOfRandomBitOfMask(&rng_);
        Cell next_move = NthNeighbor(last_move, index);
        DUMP(printf("Joseki %s->%s\n",
                    ToString(last_move).c_str(),
                    ToString(next_move).c_str()));
        ReplaceMove(i, next_move);
      }
    }
    Cell cell = playout_moves_[i];
    assert(mutable_position_.CellIsEmpty(cell));
    if (noli_me_tangere < 0) {
      int highest_num_neighbor_chains =
          mutable_position_.
          player_position(player).
          GetSizeOfNeighborChains(cell, 12);
      Cell best_cell = cell;
      for (int j = 1; j < options_->retries_of_isolated_moves &&
           i + j < size; ++j) {
        cell = playout_moves_[i + j];
        assert(mutable_position_.CellIsEmpty(cell));
        const int num_neighbor_chains =
            mutable_position_.
            player_position(player).
            GetSizeOfNeighborChains(cell, 12);
        if (num_neighbor_chains > highest_num_neighbor_chains) {
          best_cell = cell;
          highest_num_neighbor_chains = num_neighbor_chains;
        }    
      }
      ReplaceMove(i, best_cell);
      cell = best_cell;
    }
    neighbors18 = mutable_position_.Get18Neighbors(Opponent(player), cell);
    const WinningCondition victory = mutable_position_.MakeMoveFast(player, cell);
    DUMP(printf("%s", mutable_position_.MakeString(cell).c_str()));
    if (victory != kNoWinningCondition) {
      DUMP(printf("%c won in %d moves by %d\n", player["xo"], i, victory));
      if ((victory & ~(kBenzeneRing | kRing)) == 0) {
        const int ring_notice_threshold =
            options_->initial_chance_of_ring_notice +
            (options_->final_chance_of_ring_notice -
             options_->initial_chance_of_ring_notice) *
             (i - canned_moves_) / (size - canned_moves_);
        if (rng_(100) > ring_notice_threshold) {
          --noli_me_tangere;
          neighbors18 = 0ULL;
          continue;
        }
      }
      for (int j = 0; j <= i; ++j) {
        const MoveIndex jth_move =
            Position::CellToMoveIndex(playout_moves_[j]);
        assert(Position::MoveIndexToCell(jth_move) == playout_moves_[j]);
        const Player jth_player = playout_players_[j];
        if (jth_player == player)
          ++rave[jth_player][jth_move];
        else
          --rave[jth_player][jth_move];
      }
      *num_moves = i;
      return 2 * victory + player;
    }
    if (noli_me_tangere < 0 && options_->use_havannah_mate) {
      noli_me_tangere = HavannahMate(player, i);
    }
    last_move = playout_moves_[i];
    player = Opponent(player);
    --noli_me_tangere;
  }
  *num_moves = i;
  return 0;
}

int Playout::ReplaceMovesInRingFrames(Player player, int offset) {
  const PlayerPosition& pp = mutable_position_.player_position(player);
  int canned_moves = 0;
  for (int i = 0, size = pp.ring_frame_count(); i < size; ++i) {
    const unsigned* frame = pp.ring_frame(i);
    if (frame == NULL)
      continue;
    const int moves_to_win = frame[0];
    for (int j = 0; j < moves_to_win; ++j) {
      const int index = rng_(2);
      const Cell cell1 = static_cast<Cell>(frame[2 * j + index + 1]);
      ReplaceMove(canned_moves + offset + 2 * j, cell1);
      const Cell cell2 = static_cast<Cell>(frame[2 * j + 2 - index]);
      ReplaceMove(canned_moves + offset + 2 * (moves_to_win + j), cell2);
    }
    canned_moves += 4 * moves_to_win;
  }
  return canned_moves;
}

void Playout::ReplaceMove(int i, Cell cell) {
  assert(mutable_position_.CellIsEmpty(cell));
  const int j = reverse_playout_moves_[cell];
  reverse_playout_moves_[playout_moves_[i]] = j;
  reverse_playout_moves_[cell] = i;
  playout_moves_[j] = playout_moves_[i];
  playout_moves_[i] = cell;
}

void Playout::LookForMate(
    Player player, Cell cell, ChainNum current_chain, RowBitmask mask[]) {
  static const int kMyOffsets[6] = {  +31, +32, -1, +1, -32, -31 };
  const int neighborhood = mutable_position_.Get6Neighbors(player, cell);
// if (CountSetBits(neighborhood) == 1 ||
//    Position::CountNeighborGroupsWithPossibleBenzeneRings(neighborhood) == 9)
//   neighbors_.push_back(cell);
  if (mutable_position_.MoveIsWinning(player, cell, neighborhood, 0)) {
    winning_moves_[winning_move_count_][0] = TwoMoves(cell);
    ++winning_move_count_;
  } else {
    const PlayerPosition& player_position = mutable_position_.player_position(player);
    const int neighbor_groups = Position::CountNeighborGroups(neighborhood);
    if (neighbor_groups >= 2) {
      connecting_cells_.push_back(cell);
      if (options_->use_ring_detection) {
        const int set_bits = CountSetBits(neighborhood);
        for (int i = 0; i < set_bits; ++i) {
          const Cell neighbor_cell = OffsetCell(
              cell, kMyOffsets[GetIndexOfNthBit(i, neighborhood)]);
          const ChainNum neighbor_chain =
              player_position.NewestChainForCell(neighbor_cell);
          assert(neighbor_chain != 0);
          if (neighbor_chain != current_chain) {
            std::vector<Cell>& v = ring_closing_moves_[neighbor_chain];
            if (find(v.begin(), v.end(), cell) == v.end()) {
              v.push_back(cell);
            }
         }
        }
      }
    } else if (neighbor_groups == 1) {
      const unsigned edges_corners = Position::GetMaskOfEdgesAndCorners(cell);
      if (edges_corners != 0) {
        assert((edges_corners & ~(~0 << 12)) != 0);
        const Cell neighbor_cell =
            OffsetCell(cell, kMyOffsets[GetIndexOfNthBit(0, neighborhood)]);
        assert(mutable_position_.GetCell(neighbor_cell) == player + 1);
        const unsigned neighbor_edges_corners =
            player_position.EdgesCornersRingForCell(neighbor_cell);
        if (edges_corners & ~neighbor_edges_corners & ~(~0 << 12))
          connecting_cells_.push_back(cell);
      }
    }
    int local_winning_move_count = 0;
    for (int k = 0; k < 6; ++k) {
      const Cell neighbor_cell = NthNeighbor(cell, k);
      if (!mutable_position_.CellIsEmpty(neighbor_cell))
        continue;
      const XCoord neighbor_x = CellToX(neighbor_cell);
      const YCoord neighbor_y = CellToY(neighbor_cell);
      if ((mask[neighbor_y] >> neighbor_x) & 1)
        continue;
      further_neighbors_.push_back(neighbor_cell);
      const int neighbor_neighborhood =
          mutable_position_.Get6Neighbors(player, neighbor_cell) |
          kReverseNeighborhoods[k];
      if (mutable_position_.MoveIsWinning(
              player, neighbor_cell, neighbor_neighborhood, current_chain)) {
        mate_threats_.push_back(TwoMoves(neighbor_cell, cell));
        winning_moves_[winning_move_count_][local_winning_move_count % 2] =
            TwoMoves(cell, neighbor_cell);
        ++local_winning_move_count;
      }
    }
    if (local_winning_move_count >= 2)
      ++winning_move_count_;
  }
}

int Playout::ForceMateInOne(int i, int index, const TwoMoves mating_moves[2]) {
  Cell mating_move = mates_in_one_move_[index];
  if (playout_moves_[i + 1] == mating_move) {
    if (mates_in_one_move_.size() > 1) {
      mating_move =
          mates_in_one_move_[(index + 1) % mates_in_one_move_.size()];
    } else {
      return ForceMateInTwo(i, mating_moves);
    }
  }
  DUMP(printf("Mate in one\n"));
  ReplaceMove(i + 2, mating_move);
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 1]));
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 2]));
  return 2;
}

int Playout::ForceMateInTwo(int i, const TwoMoves mating_moves[2]) {
  assert(mating_moves != NULL);
  const int index = rng_(2);
  const Cell first_move_to_mate = mating_moves[index].first();
  const Cell second_move_to_mate = mating_moves[index].second();
  int next_move = playout_moves_[i + 1];
  if (next_move == first_move_to_mate || next_move == second_move_to_mate)
    return 0;
  next_move = playout_moves_[i + 3];
  if (next_move == first_move_to_mate || next_move == second_move_to_mate)
    return 0;
  DUMP(printf("Mate in two\n"));
  ReplaceMove(i + 2, first_move_to_mate);
  ReplaceMove(i + 4, second_move_to_mate);
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 1]));
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 2]));
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 3]));
  assert(mutable_position_.CellIsEmpty(playout_moves_[i + 4]));
  return 4;
}

int Playout::HavannahMate(Player player, int i) {
  mate_threats_.clear();
  connecting_cells_.clear();
  winning_move_count_ = 0;
  // neighbors_.clear();
  further_neighbors_.clear();
  ring_closing_moves_.clear();
  const ChainNum chain_num =
      mutable_position_.player_position(player).
      NewestChainForCell(playout_moves_[i]);
  RepeatForCellsAdjacentToChain(
      mutable_position_, player, chain_num, LookForMate);
  if (winning_move_count_ < 2) {
    sort(mate_threats_.begin(), mate_threats_.end());
    for (std::vector<TwoMoves>::const_iterator ita = mate_threats_.begin();
          ita != mate_threats_.end(); /**/) {
      std::vector<TwoMoves>::const_iterator itb =
          std::adjacent_find<std::vector<TwoMoves>::const_iterator>(
              ita, mate_threats_.end());
      if (itb == mate_threats_.end())
        break;
      winning_moves_[winning_move_count_][0] = *itb;
      winning_moves_[winning_move_count_][1] = *(itb + 1);
      ++winning_move_count_;
      for (ita = itb + 2; ita != mate_threats_.end(); ++ita) {
        if (*ita != *itb)
          break;
      }
    }
  }

  if (winning_move_count_ == 1 && options_->use_havannah_antimate) {
    const Cell almost_mating_move = winning_moves_[0][0].first();
    DUMP(printf("Defense against mate\n"));
    ReplaceMove(i + 1, almost_mating_move);
    assert(mutable_position_.CellIsEmpty(playout_moves_[i + 1]));
    return -1;
  }

  if (winning_move_count_ < 2) {
    for (std::map<ChainNum, std::vector<Cell> >::const_iterator it =
         ring_closing_moves_.begin();
         it != ring_closing_moves_.end(); ++it) {
      const std::vector<Cell>& v = it->second;
      assert(!v.empty());
      tmp0_.clear();
      tmp0_.push_back(v[0]);
      for (int j = 0, size = tmp0_.size(); j < size; ++j) {
        const Cell cell = tmp0_[j];
        for (int k = 0; k < 6; ++k) {
          const Cell neighbor_cell = NthNeighbor(cell, k);
          if (find(v.begin(), v.end(), neighbor_cell) != v.end() &&
              find(tmp0_.begin(), tmp0_.end(), neighbor_cell) == tmp0_.end()) {
            tmp0_.push_back(neighbor_cell);
          }
        }
      }
      tmp1_.clear();
      for (int j = 0, size = v.size(); j < size; ++j) {
        if (find(tmp0_.begin(), tmp0_.end(), v[j]) == tmp0_.end()) {
          tmp1_.push_back(v[j]);
        }
      }

      if (!tmp0_.empty() && !tmp1_.empty() &&
          (tmp0_.size() > 1 || tmp1_.size() > 1)) {
        winning_moves_[winning_move_count_][0] = TwoMoves(tmp0_[0], tmp1_[0]);
        if (tmp0_.size() > 1) {
          winning_moves_[winning_move_count_][1] = TwoMoves(tmp0_[1], tmp1_[0]);
        } else {
          winning_moves_[winning_move_count_][1] = TwoMoves(tmp1_[1], tmp0_[0]);
        }
        ++winning_move_count_;
        if (tmp0_.size() > 1 && tmp1_.size() > 1) {
          winning_moves_[winning_move_count_][0] = TwoMoves(tmp0_[1], tmp1_[1]);
          winning_moves_[winning_move_count_][1] = TwoMoves(tmp1_[1], tmp0_[1]);
          ++winning_move_count_;
          DUMP(printf("Double ring threat\n"));
        } else {
          DUMP(printf("Single ring threat\n"));
        }
      }
    }
  }

  if (winning_move_count_ >= 2) {
    mates_in_one_move_.clear();
    mates_in_two_moves_indices_.clear();
    for (int j = 0; j < winning_move_count_; ++j) {
      if (winning_moves_[j][0].has_one_move())
        mates_in_one_move_.push_back(winning_moves_[j][0].first());
      else
        mates_in_two_moves_indices_.push_back(j);
    }
    if (!mates_in_one_move_.empty()) {
      TwoMoves* mating_moves;
      if (!mates_in_two_moves_indices_.empty()) {
        mating_moves = winning_moves_[
            rng_.GetRandomElement(mates_in_two_moves_indices_)];
      } else {
        mating_moves = NULL;
      }
      const int index = rng_(mates_in_one_move_.size());
      return ForceMateInOne(i, index, mating_moves);
    } else {
      return ForceMateInTwo(i, winning_moves_[rng_(winning_move_count_)]);
    }
  }

  if (!connecting_cells_.empty()) {
    if (connecting_cells_.size() > 1) {
      if (rng_(100) < chance_of_forced_connection_) {
        const Cell connecting_cell =
            rng_.GetRandomElement(connecting_cells_);
        if (playout_moves_[i + 1] != connecting_cell) {
          DUMP(printf("Connecting move\n"));
          ReplaceMove(i + 2, connecting_cell);
          return 1;
        }
      }
    } else {
      if (rng_(100) < chance_of_connection_defense_) {
        const Cell connecting_cell = connecting_cells_[0];
        DUMP(printf("Defense against connecting move\n"));
        ReplaceMove(i + 1, connecting_cell);
      }
    }
  }
/*
  else if (!further_neighbors_.empty()) {
    if (!further_neighbors_.empty()) {
      const int next_move = rng_.GetRandomElement(further_neighbors_);
      if (playout_moves_[i + 1] != next_move) {
        DUMP(printf("Jump\n"));
        ReplaceMove(i + 2, next_move);
      }
    }
  }
*/
  return -1;
}

}  // namespace lajkonik
