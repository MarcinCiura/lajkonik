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

// The definitions of the MctsEngine class and its helpers.

#include "mcts.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <set>

#include "playout.h"
#include "rng.h"
#include "wfhashmap.h"

namespace lajkonik {
namespace {

const Hash kRootHash = 0;

// TODO(mciura)
int WonInNPlies(int n) { return -0x100 * n + INT_MAX - 0x80; }
int LostInNPlies(int n) { return 0x100 * n - INT_MAX + 0x80; }

// TODO(mciura)
int VictoryToPlies(int result) { return (INT_MAX - result) / 0x100; }
int DefeatToPlies(int result) { return (INT_MAX + result) / 0x100; }

bool IsZero(int number) { return number == 0; }

}  // namespace

//-- MctsNode ---------------------------------------------------------
class MctsNode {
 public:
  MctsNode() {}
  ~MctsNode() {}

  void Init() {
    memset(this, 0, sizeof *this);
  }

  int UpdateUcbNumSimulations(int ucb_num_simulations_increment) {
    return AtomicIncrement(&ucb_num_simulations_,
                           ucb_num_simulations_increment);
  }

  void UpdateUcbReward(int ucb_reward_increment) {
    if (ResultIsForced(ucb_reward_increment)) {
      ucb_reward_ = ucb_reward_increment;
    } else {
      AtomicIncrementIfFalse(
          &ucb_reward_, ucb_reward_increment, ResultIsForced);
    }
  }

  void UpdateRave(int rave_reward_increment,
                  int rave_num_simulations_increment) {
    assert(!ResultIsForced(rave_reward_increment));
    AtomicIncrement(&rave_reward_, rave_reward_increment);
    assert(rave_reward_ != INT_MIN);
    assert(rave_reward_ != INT_MAX);
    AtomicIncrement(&rave_num_simulations_, rave_num_simulations_increment);
    assert(rave_num_simulations_ > 0);
  }

  int ucb_reward() const { return ucb_reward_; }
  int ucb_num_simulations() const { return ucb_num_simulations_; }
  int rave_reward() const { return rave_reward_; }
  int rave_num_simulations() const { return rave_num_simulations_; }
  void set_visits_to_go(int n) { visits_to_go_ = n; }
  bool decrement_visits_to_go_if_nonzero() {
    return AtomicIncrementIfFalse(&visits_to_go_, -1, IsZero);
  }
  MoveIndex kid_to_visit() const {
    return static_cast<MoveIndex>(kid_to_visit_);
  }
  void set_kid_to_visit(MoveIndex move) { kid_to_visit_ = move; }
  float bias() const { return bias_ * (1.0f / 256.0f); }
  void set_bias(float n) { bias_ = n * 256.0f; }

  int HasForcedVictory() const { return VictoryIsForced(ucb_reward_); }
  int HasForcedDraw() const { return DrawIsForced(ucb_reward_); }
  int HasForcedDefeat() const { return DefeatIsForced(ucb_reward_); }
  int HasForcedResult() const { return ResultIsForced(ucb_reward_); }

  std::string ForcedResultToString() const {
    if (HasForcedVictory())
      return StringPrintf("defeat in %d", VictoryToPlies(ucb_reward_));
    else if (HasForcedDraw())
      return "inevitable draw";
    else if (HasForcedDefeat())
      return StringPrintf("victory in %d", DefeatToPlies(ucb_reward_));
    assert(false);
    return "";
  }

 private:
  int ucb_reward_;
  int ucb_num_simulations_;
  int rave_reward_;
  int rave_num_simulations_;
  int visits_to_go_;
  short kid_to_visit_;
  short bias_;
};

namespace {

//---------------------------------------------------------------------
#if 0
inline double FastSqrt(double x) {
  assert(x - x == 0.0);
  union {
    double dbl;
    long long i64;
  } u;
  u.dbl = x;
  u.i64 = (u.i64 >> 1) + (1LL << 61) - (1LL << 51);
  return u.dbl;
}
#endif

inline float FastSqrtf(float x) {
  // Defend against infinities and NaNs.
  assert(x - x == 0.0f);
  union {
    float flt;
    int i32;
  } u;
  u.flt = x;
  u.i32 = (u.i32 >> 1) + (1 << 29) - (1 << 22);
  return u.flt;
}

inline float ResultForNoVisits(
    const MctsNode* node, float first_play_urgency) {
  const int nr = node->rave_num_simulations();
  if (nr > 0)
    first_play_urgency += static_cast<float>(node->rave_reward()) / nr;
  return node->bias() + first_play_urgency;
}

inline float UtcHoeffding(const MctsNode* node,
                          float log_parent_simulations,
                          float /*rave_bias*/,
                          float first_play_urgency) {
  const float n = node->ucb_num_simulations();
  if (n <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  return node->ucb_reward() / n + FastSqrtf(log_parent_simulations / n);
}

inline float UtcHoeffdingSlow(const MctsNode* node,
                              float log_parent_simulations,
                              float /*rave_bias*/,
                              float first_play_urgency) {
  const float n = node->ucb_num_simulations();
  if (n <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  return node->ucb_reward() / n + sqrtf(log_parent_simulations / n);
}

inline float RaveGelly(const MctsNode* node,
                       float log_parent_simulations,
                       float rave_bias,
                       float first_play_urgency) {
  // Equivalence parameter == 3 * rave_bias.
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float nr = node->rave_num_simulations();
  assert(nr != 0);
  const float beta = FastSqrtf(rave_bias / (nu + rave_bias));
  return (1.0f - beta) * (node->ucb_reward() / nu) +
         beta * (node->rave_reward() / nr) +
         FastSqrtf(log_parent_simulations / nu);
}

inline float RaveTeytaud(const MctsNode* node,
                         float log_parent_simulations,
                         float rave_bias,
                         float first_play_urgency) {
  // Equivalence parameter == rave_bias.
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float nr = node->rave_num_simulations();
  assert(nr != 0);
  const float beta = rave_bias / (nu + rave_bias);
  return (1.0f - beta) * (node->ucb_reward() / nu) +
         beta * (node->rave_reward() / nr) +
         FastSqrtf(log_parent_simulations / nu);
}

inline float RaveSilver(const MctsNode* node,
                        float log_parent_simulations,
                        float rave_bias,
                        float first_play_urgency) {
  const float reward = node->ucb_reward();
  if (ResultIsForced(reward))
    return reward;
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float nr = node->rave_num_simulations();
  const float beta_by_nr = 1.0f / (nu + nr + rave_bias * nu * nr);
  return (1.0f - beta_by_nr * nr) * (reward / nu) +
         beta_by_nr * node->rave_reward() +
         FastSqrtf(log_parent_simulations / nu);
}

inline float RaveSilverWithProgressiveBias(const MctsNode* node,
                                           float log_parent_simulations,
                                           float rave_bias,
                                           float first_play_urgency) {
  const float reward = node->ucb_reward();
  if (ResultIsForced(reward))
    return reward;
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float nr = node->rave_num_simulations();
  const float beta_by_nr = 1.0f / (nu + nr + rave_bias * nu * nr);
  return (1.0f - beta_by_nr * nr) * (reward / nu) +
         beta_by_nr * node->rave_reward() +
         FastSqrtf(log_parent_simulations / nu) +
         node->bias() / FastSqrtf(nu);
}

inline float RaveSilverUnsimplified(const MctsNode* node,
                                    float log_parent_simulations,
                                    float rave_bias,
                                    float first_play_urgency) {
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float mu = node->ucb_reward() / nu;
  const float nr = node->rave_num_simulations();
  // TODO(mciura): Fix mu to the (0, 1) range.
  const float beta_by_nr =
      1.0f / (nu + nr + rave_bias * nu * nr / (mu * (1.0f - mu)));
  return (1.0f - beta_by_nr * nr) * mu +
         beta_by_nr * node->rave_reward() +
         FastSqrtf(log_parent_simulations / nu);
}

// Enhancements for Multi-Player Monte-Carlo Tree Search.
inline float ProgressiveHistoryNijssenWinands(const MctsNode* node,
                                              float log_parent_simulations,
                                              float rave_bias,
                                              float first_play_urgency) {
  const float nu = node->ucb_num_simulations();
  if (nu <= 0)
    return ResultForNoVisits(node, first_play_urgency);
  const float nr = node->rave_num_simulations();
  assert(nr != 0);
  assert(nu - node->ucb_reward() != 0);
  const float beta = rave_bias / (nu - node->ucb_reward());
  return (node->ucb_reward() / nu) +
         beta * (node->rave_reward() / nr) + beta +
         FastSqrtf(log_parent_simulations / nu);
}

int GetAdjustedNumSimulations(const MctsNode* node) {
  const int ucb_reward = node->ucb_reward();
  if (ResultIsForced(ucb_reward))
    return ucb_reward;
  return ucb_reward + node->ucb_num_simulations();
}

float GetNodeWinRatio(const MctsNode* node) {
  if (node->HasForcedDefeat())
    return DefeatToPlies(node->ucb_reward()) / 10000.0f;
  if (node->HasForcedDraw())
    return 0.5f;
  if (node->HasForcedVictory())
    return 1.0f - VictoryToPlies(node->ucb_reward()) / 10000.0f;
  return 0.5f + 0.5f * node->ucb_reward() / node->ucb_num_simulations();
}

std::string GetNodeInfo(int num_simulations, float win_ratio, bool negate) {
  if (negate)
    win_ratio = 1.0f - win_ratio;
  return StringPrintf("%.2f(%d)", 100.0f * win_ratio, num_simulations);
}

}  // namespace

//-- TranspositionTable -----------------------------------------------
typedef WaitFreeHashMap<Hash, MctsNode, LOG2_NUM_ENTRIES> HashMap;

class TranspositionTable {
 public:
  TranspositionTable(const MctsOptions* options, Rng* rng)
      : options_(options),
        rng_(rng) {
    mcts_strategies_[kHoeffding] = &TranspositionTable::ArgMax<UtcHoeffding>;
    mcts_strategies_[kHoeffdingSlow] =
        &TranspositionTable::ArgMax<UtcHoeffdingSlow>;
    mcts_strategies_[kGelly] = &TranspositionTable::ArgMax<RaveGelly>;
    mcts_strategies_[kTeytaud] = &TranspositionTable::ArgMax<RaveTeytaud>;
    mcts_strategies_[kSilver] = &TranspositionTable::ArgMax<RaveSilver>;
    mcts_strategies_[kSilverWithProgressiveBias] =
        &TranspositionTable::ArgMax<RaveSilverWithProgressiveBias>;
    mcts_strategies_[kSilverUnsimplified] =
        &TranspositionTable::ArgMax<RaveSilverUnsimplified>;
    mcts_strategies_[kNijssenWinands] =
        &TranspositionTable::ArgMax<ProgressiveHistoryNijssenWinands>;

    get_score_[kHoeffding] = &UtcHoeffding;
    get_score_[kHoeffdingSlow] = &UtcHoeffdingSlow;
    get_score_[kGelly] = &RaveGelly;
    get_score_[kTeytaud] = &RaveTeytaud;
    get_score_[kSilver] = &RaveSilver;
    get_score_[kSilverWithProgressiveBias] = &RaveSilverWithProgressiveBias;
    get_score_[kSilverUnsimplified] = &RaveSilverUnsimplified;
    get_score_[kNijssenWinands] = &ProgressiveHistoryNijssenWinands;
  }

  ~TranspositionTable() {}

  static void InitStaticFields() {
    nodes_ = new HashMap;
  }

  void Clear() {
    nodes_->Clear();
  }

  MctsNode* InsertKey(Hash position_hash) {
    return nodes_->InsertKey(position_hash);
  }

  MctsNode* FindNode(Hash position_hash) {
    return nodes_->FindValue(position_hash);
  }

  int node_count() const {
    return nodes_->num_elements();
  }

  bool ExpandNode(Hash position_hash,
                  Player player,
                  Position* position) {
    const Player opponent = Opponent(player);

    const bool use_mate_in_tree = options_->use_mate_in_tree;
    const bool use_antimate_in_tree = options_->use_antimate_in_tree;
    const bool use_deeper_mate_in_tree = options_->use_deeper_mate_in_tree;
    const int prior_num_simulations_base =
        options_->prior_num_simulations_base;
    const int prior_num_simulations_range =
        options_->prior_num_simulations_range;
    const int prior_reward_halfrange =
        options_->prior_reward_halfrange;

    Cell antimate_move = kZerothCell;
    int antimate_move_count = 0;
    winning_kids_.clear();
    position_ = position;
    const PlayerPosition& player_position = position->player_position(player);

    for (MoveIndex move = kZerothMove, size = position->NumAvailableMoves();
         move < size; move = NextMove(move)) {
      const Cell cell = Position::MoveIndexToCell(move);
      if (!position->CellIsEmpty(cell))
        continue;
      const Hash kid_hash =
          Position::ModifyZobristHash(position_hash, player, move);
      MctsNode* kid = InsertKey(kid_hash);
      if (kid == NULL)
        return false;

      if (use_mate_in_tree) {
        const int neighborhood = position->Get6Neighbors(player, cell);
        if (position->MoveIsWinning(player, cell, neighborhood, 0)) {
          MctsNode* node = FindNode(position_hash);
          assert(node != NULL);
          kid->UpdateUcbReward(WonInNPlies(0));
          node->UpdateUcbReward(LostInNPlies(1));
          return true;
        }
      }

      if (use_antimate_in_tree) {
        const int opponent_neighborhood =
            position->Get6Neighbors(opponent, cell);
        if (position->MoveIsWinning(opponent, cell, opponent_neighborhood, 0)) {
          antimate_move = cell;
          ++antimate_move_count;
        }
      }

      if (use_deeper_mate_in_tree) {
        position->MakeMoveReversibly(player, cell, &memento_);
        winning_move_count_ = 0;
        const PlayerPosition& pp = position->player_position(player);
        const ChainNum current_chain = pp.chain_for_cell(cell);
        RepeatForCellsAdjacentToChain(
            *position, player, current_chain, LookForMate);
        memento_.UndoAll();
        // Defer kid->UpdateUcbReward(WonInNPlies(2)) after antimate to prevent
        // opponent's victory in 2 from shadowing player's victory in 1.
        if (winning_move_count_ >= 2) {
          winning_kids_.push_back(kid);
          continue;
        }
        // } else if (winning_move_count_ == 1) {
        //   TODO(mciura): update kid(kid)?
        // }
      }

/*
      static const int k10Offsets[6] = { -1, -32, -31, +1, +32, +31 };

      Cell prev_neighbor_cell = OffsetCell(cell, k10Offsets[4]);
      Cell curr_neighbor_cell = OffsetCell(cell, k10Offsets[5]);
      int two_bridge_bias = 0;
      for (int j = 0; j < 6; ++j) {
        Cell next_neighbor_cell = OffsetCell(cell, k10Offsets[j]);
        if (position->GetCell(curr_neighbor_cell) == opponent + 1 &&
            (position->GetCell(prev_neighbor_cell) & (player + 1)) != 0 &&
            (position->GetCell(next_neighbor_cell) & (player + 1)) != 0) {
          two_bridge_bias += options_->two_bridge_defense_bias;
        }
        if (position->GetCell(curr_neighbor_cell) == player + 1 &&
            (position->GetCell(prev_neighbor_cell) & (opponent + 1)) != 0 &&
            (position->GetCell(next_neighbor_cell) & (opponent + 1)) != 0) {
          two_bridge_bias += options_->two_bridge_cut_bias;
        }
        prev_neighbor_cell = curr_neighbor_cell;
        curr_neighbor_cell = next_neighbor_cell;
      }
      if (two_bridge_bias != 0) {
        kid->UpdateUcbNumSimulations(two_bridge_bias);
        kid->UpdateUcbReward(two_bridge_bias);
        kid->UpdateRave(two_bridge_bias, two_bridge_bias);
      }

      int bias = 0;
      if (options_->two_bridge_bias != 0 &&
          position->MoveCreatesTwoBridge(player, cell)) {
        bias += options_->two_bridge_bias;
      }
      if (options_->neighborhood_bias != 0 &&
          position->Get6Neighbors(player, cell) > 0) {
        bias += options_->neighborhood_bias;
      }
      if (Position::CountNeighborGroups(
              position->Get6Neighbors(player, cell)) > 1) {
        if (position->Get6Neighbors(Opponent(player), cell) > 0) {
          if (options_->forced_join_bias != 0) {
            kid->add_bias(options_->forced_join_bias);
          }
        } else {
          if (options_->spontaneous_join_bias != 0) {
            kid->add_bias(options_->spontaneous_join_bias);
          }
        }
      }
      if (bias != 0)
        kid->set_bias(bias);
*/
      float bias;
      if (options_->chain_size_bias_factor != 0.0) {
        bias =
            options_->chain_size_bias_factor *
            player_position.GetSizeOfNeighborChains(
                cell, 6 * options_->neighborhood_size);
      } else {
        bias = 0.0;
      }
      if (options_->locality_bias != 0.0) {
        unsigned neighbors =
            player_position.Get18Neighbors(cell) & kAndTo12Neighbors;
        if (neighbors != 0) {
          bias += options_->locality_bias;
        }
      }
      kid->set_bias(bias);

      if (options_->use_rave_randomization) {
        kid->UpdateRave(
            (*rng_)(2 * prior_reward_halfrange + 1) -
                prior_reward_halfrange,
            (*rng_)(prior_num_simulations_range) +
                prior_num_simulations_base);
      } else {
        kid->UpdateUcbReward(
            (*rng_)(2 * prior_reward_halfrange + 1) -
            prior_reward_halfrange);
        kid->UpdateUcbNumSimulations(
            (*rng_)(prior_num_simulations_range) +
            prior_num_simulations_base);
      }
    }
    if (antimate_move_count > 1) {
      // Without the test for position_hash != kRootHash, player's
      // defeat in 2 would not end the controller's search early.
      if (position_hash != kRootHash) {
        MctsNode* node = FindNode(position_hash);
        assert(node != NULL);
        node->UpdateUcbReward(WonInNPlies(2));
        return true;
      }
    } else if (antimate_move_count == 1) {
      for (MoveIndex move = kZerothMove;
           move < position->NumAvailableMoves(); move = NextMove(move)) {
        const Cell cell = Position::MoveIndexToCell(move);
        if (!position->CellIsEmpty(cell) || cell == antimate_move)
          continue;
        const Hash kid_hash =
            Position::ModifyZobristHash(position_hash, player, move);
        MctsNode* kid = FindNode(kid_hash);
        assert(kid != NULL);
        kid->UpdateUcbReward(LostInNPlies(1));
      }
    }
    // TODO(mciura): Make these updates atomic.
    for (int i = 0, size = winning_kids_.size(); i < size; ++i) {
      if (!ResultIsForced(winning_kids_[i]->ucb_reward())) {
        winning_kids_[i]->UpdateUcbReward(WonInNPlies(2));
      }
    }
    return true;
  }

  void GetTwoMostSimulatedKids(Hash position_hash,
                               Player player,
                               MoveIndex move_count,
                               MoveInfo* kid_1,
                               MoveInfo* kid_2) const {
    kid_1->move = kid_2->move = kInvalidMove;
    kid_1->num_simulations = kid_2->num_simulations = INT_MIN;
    kid_1->win_ratio = kid_2->win_ratio = NAN;
    for (MoveIndex move = kZerothMove; move < move_count;
         move = NextMove(move)) {
      const Hash kid_position_hash =
          Position::ModifyZobristHash(position_hash, player, move);
      const MctsNode* kid = FindNode(kid_position_hash);
      if (kid != NULL) {
        const int num_simulations = GetAdjustedNumSimulations(kid);
        assert(num_simulations > INT_MIN);
        if (num_simulations > kid_1->num_simulations) {
          *kid_2 = *kid_1;
          kid_1->move = move;
          kid_1->num_simulations = num_simulations;
          kid_1->win_ratio = GetNodeWinRatio(kid);
        } else if (num_simulations > kid_2->num_simulations) {
          kid_2->move = move;
          kid_2->num_simulations = num_simulations;
          kid_2->win_ratio = GetNodeWinRatio(kid);
        }
      }
    }
  }

  MctsNode* SelectKidForExploration(Hash position_hash,
                                    const MctsNode* node,
                                    Player player,
                                    MoveIndex move_count,
                                    MoveIndex* kid_index,
                                    Hash* kid_position_hash,
                                    bool* has_forced_result) {
    return (this->*mcts_strategies_[options_->exploration_strategy])(
        position_hash, node, player, move_count,
        kid_index, kid_position_hash, has_forced_result);
  }

  void PrintDebugInfo(Player player, const Position& position) {
    Hash position_hash = kRootHash;
    MctsNode* root = InsertKey(position_hash);
    if (root == NULL)
      root = nodes_->FindValue(position_hash);
    assert(root != NULL);
    if (root->HasForcedResult())
      fprintf(stderr, "%s\n", root->ForcedResultToString().c_str());
    std::string result = StringPrintf(
        "%c %d ", player["xo"], nodes_->num_elements());
    result += GetNodeInfo(root->ucb_num_simulations(),
                          GetNodeWinRatio(root), true);
    result += '\n';

    MoveInfo kid_1;
    MoveInfo kid_2;
    std::string appendix;
    GetTwoMostSimulatedKids(
        position_hash, player, position.NumAvailableMoves(), &kid_1, &kid_2);
    if (kid_2.move != kInvalidMove) {
      appendix += ToString(Position::MoveIndexToCell(kid_2.move));
      appendix += ':';
      appendix += GetNodeInfo(kid_2.num_simulations, kid_2.win_ratio, 0);
    }
    for (int i = 0; /**/; ++i) {
      GetTwoMostSimulatedKids(
          position_hash, player, position.NumAvailableMoves(), &kid_1, &kid_2);
      if (kid_1.move == kInvalidMove)
        break;
      if (kid_1.num_simulations <= 100 &&
          !ResultIsForced(kid_1.num_simulations) && i > 0)
        break;
      result += ToString(Position::MoveIndexToCell(kid_1.move));
      result += ':';
      result += GetNodeInfo(kid_1.num_simulations, kid_1.win_ratio, i % 2);
      result += ' ';
      position_hash =
          Position::ModifyZobristHash(position_hash, player, kid_1.move);
      player = Opponent(player);
    }
    fprintf(stderr, "%s/ %s\n", result.c_str(), appendix.c_str());
  }

  void DumpToHtml(Hash position_hash,
                  Player player,
                  const Position& position,
                  FILE* file) {
    fprintf(
        file,
        "<html>\n<head>\n"
        "<script type=\"text/javascript\" src=\"havannah.js\"></script>\n"
        "</head>\n<body onload=\"");
    struct {
      float ucb_win_ratio;
      int ucb_num_simulations;
      float rave_win_ratio;
      int rave_num_simulations;
    } board_info[kNumMovesOnBoard],
    max_values = { 0.0f, 0, 0.0f, 0 },
    min_values = { 100.0f, INT_MAX, 100.0f, INT_MAX };

    int last_move = Position::MoveIndexToCell(position.NumAvailableMoves());

    for (MoveIndex move = kZerothMove; move < kNumMovesOnBoard;
         move = NextMove(move)) {
      const Cell cell = Position::MoveIndexToCell(move);
      if (!position.CellIsEmpty(cell) && cell != last_move)
        continue;
      const MoveIndex move2 = Position::CellToMoveIndex(cell);
      assert(move2 >= kZerothMove);
      assert(move2 < kNumMovesOnBoard);
      const Hash kid_position_hash =
          Position::ModifyZobristHash(position_hash, player, move2);
      const MctsNode* kid = FindNode(kid_position_hash);
      if (kid == NULL)
        continue;
      const float ucb_win_ratio = 100.0f * GetNodeWinRatio(kid);
      const float rave_win_ratio =
          50.0f + 50.0f * kid->rave_reward() /
          (kid->rave_num_simulations() + 1);
      max_values.ucb_win_ratio = std::max(
          max_values.ucb_win_ratio, ucb_win_ratio);
      min_values.ucb_win_ratio = std::min(
          min_values.ucb_win_ratio, ucb_win_ratio);
      max_values.ucb_num_simulations = std::max(
          max_values.ucb_num_simulations, kid->ucb_num_simulations());
      max_values.rave_win_ratio = std::max(
          max_values.rave_win_ratio, rave_win_ratio);
      min_values.rave_win_ratio = std::min(
          min_values.rave_win_ratio, rave_win_ratio);
      min_values.rave_num_simulations = std::min(
          min_values.rave_num_simulations, kid->rave_num_simulations());
      max_values.rave_num_simulations = std::max(
          max_values.rave_num_simulations, kid->rave_num_simulations());
      board_info[move2].ucb_win_ratio = ucb_win_ratio;
      board_info[move2].ucb_num_simulations = kid->ucb_num_simulations();
      board_info[move2].rave_win_ratio = rave_win_ratio;
      board_info[move2].rave_num_simulations = kid->rave_num_simulations();
    }

    fprintf(file, "drawBoard('UCB win','%.1f-%.1f',%d,%d,'",
            min_values.ucb_win_ratio, max_values.ucb_win_ratio,
            SIDE_LENGTH, player);

    for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
      for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        if (position.CellIsEmpty(cell) || cell == last_move) {
          fprintf(file, "%.f ",
                  100.0f *
                  (board_info[move].ucb_win_ratio - min_values.ucb_win_ratio) /
                  (max_values.ucb_win_ratio - min_values.ucb_win_ratio));
        } else {
          fprintf(file, "%c ", position.GetCell(cell)["?wb"]);
        }
      }
    }
    fprintf(file, "');");

    fprintf(file, "drawBoard('UCB moves','0-%d',%d,%d,'",
            max_values.ucb_num_simulations, SIDE_LENGTH, player);
    for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
      for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        if (position.CellIsEmpty(cell) || cell == last_move) {
          fprintf(file, "%.f ",
                  100.0f * sqrt(board_info[move].ucb_num_simulations) /
                  sqrt(max_values.ucb_num_simulations));
        } else {
          fprintf(file, "%c ", position.GetCell(cell)["?wb"]);
        }
      }
    }
    fprintf(file, "');");

    fprintf(file, "drawBoard('RAVE win','%.1f-%.1f',%d,%d,'",
            min_values.rave_win_ratio, max_values.rave_win_ratio,
            SIDE_LENGTH, player);
    for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
      for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        if (position.CellIsEmpty(cell) || cell == last_move) {
          fprintf(
              file, "%.f ",
              100.0f *
              (board_info[move].rave_win_ratio - min_values.rave_win_ratio) /
              (max_values.rave_win_ratio - min_values.rave_win_ratio));
        } else {
          fprintf(file, "%c ", position.GetCell(cell)["?wb"]);
        }
      }
    }
    fprintf(file, "');");

    fprintf(file, "drawBoard('RAVE moves','%d-%d',%d,%d,'",
            min_values.rave_num_simulations,
            max_values.rave_num_simulations,
            SIDE_LENGTH, player);

    for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
      for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        if (position.CellIsEmpty(cell) || cell == last_move) {
          fprintf(file, "%.f ",
                  100.0f * sqrt(board_info[move].rave_num_simulations -
                                min_values.rave_num_simulations) /
                  sqrt(max_values.rave_num_simulations -
                       min_values.rave_num_simulations));
        } else {
          fprintf(file, "%c ", position.GetCell(cell)["?wb"]);
        }
      }
    }
    fprintf(file, "');");

    fprintf(
        file,
        "\">\n"
        "<canvas id=\"UCB win\"></canvas>\n"
        "<canvas id=\"UCB moves\"></canvas><br>\n"
        "<canvas id=\"RAVE win\"></canvas>\n"
        "<canvas id=\"RAVE moves\"></canvas>\n"
        "</body>\n</html>");
  }

  void DumpGameTree(Hash position_hash,
                    Player player,
                    int depth,
                    int parent_simulations,
                    const std::string& prefix,
                    const Position& position,
                    FILE* file) {
    if (depth < 0)
      return;
    const MctsNode* node = FindNode(position_hash);
    if (node == NULL)
      return;
    fprintf(file, "%s %s\t%s\t%.7g\n",
            prefix.c_str(),
            GetNodeInfo(
                node->ucb_num_simulations(),
                GetNodeWinRatio(node),
                false).c_str(),
            GetNodeInfo(
                node->rave_num_simulations(),
                0.5 + 0.5 * node->rave_reward() / node->rave_num_simulations(),
                false).c_str(),
            get_score_[options_->exploration_strategy](
                node,
                options_->exploration_factor * logf(parent_simulations),
                options_->rave_bias,
                options_->first_play_urgency));
    for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
      for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        assert(move >= kZerothMove);
        assert(move < kNumMovesOnBoard);
        const Hash kid_position_hash =
            Position::ModifyZobristHash(position_hash, player, move);
        std::string new_prefix = "  " + prefix + ' ' + ToString(cell);
        if (position.CellIsEmpty(cell))
          new_prefix += '.';
        else
          new_prefix += '#';
        DumpGameTree(kid_position_hash, Opponent(player), depth - 1,
                     node->ucb_num_simulations(),
                     new_prefix.c_str(), position, file);
      }
    }
  }

  Hash GetStatus(Hash position_hash,
                 Player player,
                 const Position& start_position,
                 std::string* status) const {
    int board_info[kNumMovesOnBoard];
    int max_num_simulations = 0;
    Hash best_move_hash = kRootHash;
    for (MoveIndex move = kZerothMove; move < kNumMovesOnBoard;
         move = NextMove(move)) {
      const Cell cell = Position::MoveIndexToCell(move);
      const MoveIndex move2 = Position::CellToMoveIndex(cell);
      assert(move2 >= kZerothMove);
      assert(move2 < kNumMovesOnBoard);
      const Hash kid_position_hash =
          Position::ModifyZobristHash(position_hash, player, move2);
      const MctsNode* kid = FindNode(kid_position_hash);
      if (kid != NULL)
        board_info[move2] = kid->ucb_num_simulations();
      else
        board_info[move2] = 0;
      if (board_info[move2] > max_num_simulations) {
        max_num_simulations = board_info[move2];
        best_move_hash = kid_position_hash;
      }
    }
    const float sqrt_max_num_simulations = sqrt(max_num_simulations);
    std::string result;
    for (XCoord x = kGapLeft; x < kPastColumns; x = NextX(x)) {
      for (YCoord y = kLastRow; y >= kGapAround; y = PrevY(y)) {
        if (!LiesOnBoard(x, y))
          continue;
        const Cell cell = XYToCell(x, y);
        const MoveIndex move = Position::CellToMoveIndex(cell);
        assert(move >= kZerothMove);
        assert(move < kNumMovesOnBoard);
        if (start_position.CellIsEmpty(cell)) {
          *status += StringPrintf(
              "%.f ", (board_info[move] == 0) ?
                  0.0f :
                  100.0f * sqrt(board_info[move]) / sqrt_max_num_simulations);
        } else {
          *status += StringPrintf("%c ", start_position.GetCell(cell)["?wb"]);
        }
      }
    }
    return best_move_hash;
  }

  void GetPositions(
      Player player,
      const Position& position,
      int lower,
      int upper,
      std::vector<std::vector<Cell> >* cell_list) const {
    std::vector<Cell> cells;
    std::set<Hash> dumped;
    GetPositionsHelper(
        player, position, kRootHash, lower, upper, cell_list, &cells, &dumped);
  }

 private:
  //
  typedef float (*GetScore)(const MctsNode*, float, float, float);

  const MctsNode* FindNode(Hash position_hash) const {
    return nodes_->FindValue(position_hash);
  }

  void GetPositionsHelper(
      Player player,
      const Position& position,
      Hash position_hash,
      int lower,
      int upper,
      std::vector<std::vector<Cell> >* cell_list,
      std::vector<Cell>* cells,
      std::set<Hash>* dumped) const {
    const MctsNode* node = FindNode(position_hash);
    if (node->ucb_num_simulations() < lower) {
      return;
    } else if (node->ucb_num_simulations() > upper) {
      cells->push_back(kZerothCell);
      for (MoveIndex move = kZerothMove; move < kNumMovesOnBoard;
           move = NextMove(move)) {
        const Hash kid_position_hash =
            Position::ModifyZobristHash(position_hash, player, move);
        const MctsNode* kid = FindNode(kid_position_hash);
        if (kid != NULL) {
          cells->back() = Position::MoveIndexToCell(move);
          GetPositionsHelper(
              Opponent(player), position, kid_position_hash,
              lower, upper, cell_list, cells, dumped);
        }
      }
      cells->pop_back();
    } else {
      if (dumped->find(position_hash) == dumped->end()) {
        dumped->insert(position_hash);
        cell_list->push_back(*cells);
      }
    }
  }

  // TODO(mciura)
  template<GetScore get_score>
  MctsNode* ArgMax(Hash position_hash,
                   const MctsNode* node,
                   Player player,
                   MoveIndex move_count,
                   MoveIndex* kid_index,
                   Hash* kid_position_hash,
                   bool* has_forced_result) {
    assert(node != NULL);
    const int num_simulations = node->ucb_num_simulations();
    assert(num_simulations > 0);
    const float log_parent_simulations =
        options_->exploration_factor * logf(num_simulations);
    const float rave_bias = options_->rave_bias;
    const float first_play_urgency = options_->first_play_urgency;
    float best_value = -FLT_MAX;
    MctsNode* best_kid = NULL;
    for (MoveIndex move = kZerothMove; move < move_count;
         move = NextMove(move)) {
      const Hash ith_kid_position_hash =
          Position::ModifyZobristHash(position_hash, player, move);
      MctsNode* kid = FindNode(ith_kid_position_hash);
      if (kid != NULL) {
        float value = get_score(
            kid, log_parent_simulations, rave_bias, first_play_urgency);
        if (value > best_value) {
          best_value = value;
          best_kid = kid;
          *kid_index = move;
          *kid_position_hash = ith_kid_position_hash;
          *has_forced_result = ResultIsForced(value);
        }
      }
    }
    return best_kid;
  }

  void LookForMate(
      Player player,
      Cell cell,
      ChainNum current_chain,
      RowBitmask mask[]) {
    (void)current_chain;
    (void)mask;
    const int neighborhood = position_->Get6Neighbors(player, cell);
    if (position_->MoveIsWinning(player, cell, neighborhood, 0)) {
      ++winning_move_count_;
    }
  }

  // TODO(mciura)
  static HashMap* nodes_;

  MctsNode* (TranspositionTable::*mcts_strategies_[kNumStrategies])(
      Hash, const MctsNode*, Player, MoveIndex, MoveIndex*, Hash*, bool*);

  float (*get_score_[kNumStrategies])(const MctsNode*, float, float, float);
  
  const MctsOptions* options_;

  Rng* rng_;

  Memento memento_;

  std::vector<MctsNode*> winning_kids_;

  const Position* position_;

  int winning_move_count_;

  TranspositionTable(const TranspositionTable&);
  void operator=(const TranspositionTable&);
};

HashMap* TranspositionTable::nodes_;

namespace {

struct InitModule {
  InitModule() { TranspositionTable::InitStaticFields(); }
} init_module;

}  // namespace

//-- MctsEngine -------------------------------------------------------
MctsEngine::MctsEngine(MctsOptions* options, Playout* playout)
    : transposition_table_(
          new TranspositionTable(options, playout->rng())),
      playout_(playout),
      options_(options) {
  // So that DumpGameTree() works before any move.
  position_.InitToStartPosition();
}

MctsEngine::~MctsEngine() {
  delete transposition_table_;
}

void MctsEngine::UpdateRaveInTree(Hash position_hash,
                                  Player player,
                                  int move_index,
                                  int reward,
                                  int num_simulations) {
  for (MoveIndex move = kZerothMove, size = position_.NumAvailableMoves();
       move < size; move = NextMove(move)) {
    if (rave_[player][move] != 0) {
      const Hash kid_position_hash =
          Position::ModifyZobristHash(position_hash, player, move);
      MctsNode* kid = transposition_table_->InsertKey(kid_position_hash);
      if (kid == NULL)
        return;
      kid->UpdateRave(rave_[player][move], num_simulations);
    }
  }
  for (int i = move_index, end = moves_.size(); i < end; i += 2) {
    const Hash kid_position_hash =
        Position::ModifyZobristHash(
            position_hash, player, Position::CellToMoveIndex(moves_[i]));
    MctsNode* kid = transposition_table_->InsertKey(kid_position_hash);
    if (kid == NULL)
      return;
    kid->UpdateRave(-reward, num_simulations);
  }
}

int MctsEngine::GetPlayoutResult(
    Player player, Cell last_move, int empty_cell_count) {
  int sum = 0;
  for (int i = options_->play_n_playouts_at_once; i > 0; --i) {
    int num_moves;
    const int result = playout_->Play(player, last_move, rave_, &num_moves);
    stats_[empty_cell_count].Add(num_moves);
    if (result != 0)
      sum += 2 * ((result % 2) ^ player) - 1;
  }
  return sum;
}

int MctsEngine::Descend(Hash position_hash,
                        MctsNode* node,
                        Player player,
                        Cell last_move,
                        int empty_cell_count) {
  MctsNode* kid;
  Hash kid_position_hash;
  MoveIndex kid_index;
  const bool nonzero_visits_left = node->decrement_visits_to_go_if_nonzero();
  if (nonzero_visits_left) {
    kid_index = node->kid_to_visit();
    kid_position_hash =
        Position::ModifyZobristHash(position_hash, player, kid_index);
    kid = transposition_table_->FindNode(kid_position_hash);
    if (kid != NULL && kid->HasForcedResult())
      node->set_visits_to_go(0);    
  } else {
    bool has_forced_result = false;
    kid = transposition_table_->SelectKidForExploration(
        position_hash,
        node,
        player,
        position_.NumAvailableMoves(),
        &kid_index,
        &kid_position_hash,
        &has_forced_result);
    if (!has_forced_result) {
      if (kid != NULL) {
        node->set_kid_to_visit(kid_index);
        node->set_visits_to_go(
            options_->tricky_epsilon * kid->ucb_num_simulations() + 1);
      }
    } else {
      const int reward = kid->ucb_reward();
      if (DefeatIsForced(reward))
        return WonInNPlies(DefeatToPlies(reward) + 1);
      else if (DrawIsForced(reward))
        return kBoardFilledDraw;
      else if (VictoryIsForced(reward))
        return LostInNPlies(VictoryToPlies(reward) + 1);
      else
        assert(false);
    }
  }
  if (kid != NULL) {
    const Cell cell = Position::MoveIndexToCell(kid_index);
    assert(position_.CellIsEmpty(cell));
    const int result = position_.MakeMoveReversibly(player, cell, &memento_);
    if (result != 0) {
      if (options_->use_solver) {
        kid->UpdateUcbReward(WonInNPlies(0));
        return LostInNPlies(1);
      } else {
        kid->UpdateUcbReward(+1);
        return -1;
      }
    } else {
      moves_.push_back(cell);
      const int reward = UpdateNodeAndGetReward(
          kid_position_hash, kid, Opponent(player),
          cell, empty_cell_count - 1);
      if (DefeatIsForced(reward))
        return +1;
      else if (DrawIsForced(reward))
        return 0;
      else if (VictoryIsForced(reward))
        return LostInNPlies(VictoryToPlies(reward) + 1);
      return -reward;
    }
  } else {
    return GetPlayoutResult(player, last_move, empty_cell_count);
  }
}

int MctsEngine::UpdateNodeAndGetReward(Hash position_hash,
                                       MctsNode* node,
                                       Player player,
                                       Cell last_move,
                                       int empty_cell_count) {
  assert(node != NULL);
  int reward = node->ucb_reward();  
  if (ResultIsForced(reward))
    return reward;
  int num_simulations;
  if (options_->use_virtual_loss) {
    num_simulations = node->UpdateUcbNumSimulations(
        options_->play_n_playouts_at_once);
  } else {
    num_simulations = node->ucb_num_simulations();
  }
  const int current_move_index = moves_.size();
  if (empty_cell_count == 0) {
    empty_cell_count_at_bottom_ = 0;
    reward = kBoardFilledDraw;
  } else if (num_simulations < options_->expand_after_n_playouts) {
    empty_cell_count_at_bottom_ = empty_cell_count;
    reward = GetPlayoutResult(player, last_move, empty_cell_count);
  } else if (num_simulations == options_->expand_after_n_playouts) {
    if (transposition_table_->ExpandNode(position_hash, player, &position_)) {
      reward = Descend(
          position_hash, node, player, last_move, empty_cell_count);
    } else {
      empty_cell_count_at_bottom_ = empty_cell_count;
      reward = GetPlayoutResult(player, last_move, empty_cell_count);
    }
  } else {
    reward = Descend(position_hash, node, player, last_move, empty_cell_count);
  }
  if (!ResultIsForced(reward) &&
      empty_cell_count - empty_cell_count_at_bottom_ <=
          options_->rave_update_depth) {
    UpdateRaveInTree(position_hash, player, current_move_index, reward,
                     options_->play_n_playouts_at_once);
  }
  if (!options_->use_virtual_loss)
    node->UpdateUcbNumSimulations(options_->play_n_playouts_at_once);
  node->UpdateUcbReward(reward);
  return reward;
}

void MctsEngine::ClearTranspositionTable() {
  transposition_table_->Clear();
}

void MctsEngine::SearchForMove(Player player,
                               const Position& start_position,
                               volatile bool* terminate) {
  player_ = player;
  position_.CopyFrom(start_position);
  playout_->PrepareForPlayingFromPosition(&position_);
#if 0
  for (int i = 0; i <= num_available_moves; ++i) {
    stats_[i].Init();
  }
#endif
  const int num_available_moves = start_position.NumAvailableMoves();
  const Cell last_move = start_position.MoveNPliesAgo(0);
  MctsNode* root = transposition_table_->InsertKey(kRootHash);
  assert(root != NULL);
  is_running_ = true;
  for (int i = 1; !*terminate && !root->HasForcedResult(); ++i) {
    moves_.clear();
    memset(rave_, 0, sizeof rave_);
    UpdateNodeAndGetReward(
        kRootHash, root, player, last_move,
        num_available_moves);
    memento_.UndoAll();
  }
#if 0
  fprintf(stderr, "\n");
  for (int i = 0; i <= num_available_moves; ++i) {
    const Statistics& s = stats_[num_available_moves - i];
    if (s.N() != 0) {
      fprintf(stderr, "%d: %lf ± %lf (%d)\n",
              i, s.Mean(), s.StdDev(), s.N());
    }
  }
#endif
}

void MctsEngine::GetTwoBestMoves(MoveInfo* move_1, MoveInfo* move_2) const {
  transposition_table_->GetTwoMostSimulatedKids(
      kRootHash, player_, position_.NumAvailableMoves(), move_1, move_2);
}

void MctsEngine::PrintDebugInfo(int sec) {
  fprintf(stderr, "\n%d:%02d ", sec / 60, sec % 60);
  if (is_running())
    transposition_table_->PrintDebugInfo(player_, position_);
  else
    fprintf(stderr, "(waiting)");
}

bool MctsEngine::DumpGameTree(
    int depth, const std::string& filename, std::string* error) const {
  FILE* file = fopen(filename.c_str(), "wt");
  if (file == NULL) {
    *error = StringPrintf("Cannot open file %s", filename.c_str());
    return false;
  }
  if (filename.size() > 5 &&
      filename.substr(filename.size() - 5) == ".html") {
    transposition_table_->DumpToHtml(kRootHash, player_, position_, file);
  } else {
    transposition_table_->DumpGameTree(
        kRootHash, player_, depth, 1, "", position_, file);
  }
  if (fclose(file) != 0) {
    *error = StringPrintf("Cannot close file %s", filename.c_str());
    return false;
  }
  return true;
}

void MctsEngine::GetStatus(const Position& start_position,
                           std::string* first_status,
                           std::string* second_status) const {
  first_status->clear();
  second_status->clear();
  Hash best_move_hash = transposition_table_->GetStatus(
      kRootHash, player_,
      start_position, first_status);
  transposition_table_->GetStatus(
      best_move_hash, Opponent(player_),
      start_position, second_status);
}

void MctsEngine::GetPositions(
    int lower, int upper, std::vector<std::vector<Cell> >* cell_list) const {
  transposition_table_->GetPositions(
      player_, position_, lower, upper, cell_list);
}

void MctsEngine::GetSgf(int threshold, std::string* sgf) const {
  *sgf = StringPrintf("(;FF[4]SZ[%d]", SIDE_LENGTH);
  for (MoveIndex move = kZerothMove; move < position_.NumAvailableMoves();
       move = NextMove(move)) {  
    RecursiveGetSgf(
        player_,
        Position::ModifyZobristHash(kRootHash, player_, move),
        Position::MoveIndexToCell(move),
        threshold,
        sgf);
  }
  *sgf += ')';
}

void MctsEngine::RecursiveGetSgf(
    Player player,
    Hash hash,
    Cell cell,
    int threshold,
    std::string* sgf) const {
  const MctsNode* node = transposition_table_->FindNode(hash);
  if (node == NULL)
    return;
  const int ucb_num_simulations = node->ucb_num_simulations();
  if (ucb_num_simulations < threshold)
    return;
  const int ucb_reward = node->ucb_reward();
  *sgf += StringPrintf(
      "(;%c[%s]C[%d/%d]\n",
      player["WB"],
      ToString(cell).c_str(),
      ucb_reward + ucb_num_simulations,
      ucb_num_simulations);
  for (MoveIndex move = kZerothMove; move < position_.NumAvailableMoves();
       move = NextMove(move)) {  
    RecursiveGetSgf(
        Opponent(player),
        Position::ModifyZobristHash(hash, Opponent(player), move),
        Position::MoveIndexToCell(move),
        threshold,
        sgf);
  }
  *sgf += ')';
}

int MctsEngine::node_count() const {
  return transposition_table_->node_count();
}

PlayoutOptions* MctsEngine::playout_options() {
  return playout_->options();
}

}  // namespace lajkonik
