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

// The main module for comparing player strategies.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>

#include "controller.h"
#include "define-playout-patterns.h"
#include "havannah.h"
#include "mcts.h"
#include "playout.h"

namespace {

float ComparePlayers(const lajkonik::ControllerOptions options[2],
                     const std::vector<lajkonik::MctsEngine*> engines[2],
                     lajkonik::Player player) {
  lajkonik::Controller* controllers[2];
  controllers[0] = new lajkonik::Controller(options[0], engines[0]);
  controllers[1] = new lajkonik::Controller(options[1], engines[1]);

  while (true) {
    controllers[player]->ClearTranspositionTable();
    const std::string move = controllers[player]->SuggestMove(player, 0);
    if (move == "pass") {
      delete controllers[0];
      delete controllers[1];
      return 0.5;
    } else if (move == "swap") {
      std::swap(controllers[0], controllers[1]);
    }
    int result;
    if (!controllers[player]->MakeMove(player, move, &result)) {
      fprintf(stderr, "Unexpected move %s\n", move.c_str());
      exit(EXIT_FAILURE);
    }
    const lajkonik::Position& p = controllers[player]->position();
    fprintf(stderr, "%s\n", p.MakeString(p.MoveNPliesAgo(0)).c_str());
    if (result != lajkonik::kNoneWon) {
      float winner = -1;
      switch (result) {
        case lajkonik::kWhiteWon:
          controllers[lajkonik::kBlack]->LogDebugInfo(lajkonik::kBlack);
          winner = lajkonik::kWhite;
          break;
        case lajkonik::kDraw:
          controllers[lajkonik::kWhite]->LogDebugInfo(lajkonik::kWhite);
          controllers[lajkonik::kBlack]->LogDebugInfo(lajkonik::kBlack);
          winner = 0.5;
          break;
        case lajkonik::kBlackWon:
          controllers[lajkonik::kWhite]->LogDebugInfo(lajkonik::kWhite);
          winner = lajkonik::kBlack;
          break;
        default:
          assert(false);
          break;
      }
      delete controllers[0];
      delete controllers[1];
      return winner;
    }
    if (!controllers[Opponent(player)]->MakeMove(player, move, &result)) {
      fprintf(stderr, "Unexpected move %s\n", move.c_str());
      exit(EXIT_FAILURE);
    }
    player = Opponent(player);
  }
}

}  // namespace

int main() {
  using lajkonik::kWhite;
  using lajkonik::kBlack;

  lajkonik::PlayoutOptions prototype_playout_options;
  lajkonik::PlayoutOptions playout_options[2];

  prototype_playout_options.initial_chance_of_ring_notice = 150.0;
  prototype_playout_options.final_chance_of_ring_notice = -350.0;
  prototype_playout_options.chance_of_forced_connection_intercept = 34.0;
  prototype_playout_options.chance_of_forced_connection_slope = -30.0;
  prototype_playout_options.chance_of_connection_defense_intercept = 42.0;
  prototype_playout_options.chance_of_connection_defense_slope = -28.0;
  prototype_playout_options.retries_of_isolated_moves = 1;
  prototype_playout_options.use_havannah_mate = true;
  prototype_playout_options.use_havannah_antimate = true;
  prototype_playout_options.use_ring_detection = true;

  playout_options[kWhite] = prototype_playout_options;
  playout_options[kBlack] = prototype_playout_options;
  playout_options[kBlack].retries_of_isolated_moves = 5;

  lajkonik::Patterns white_patterns(lajkonik::kPlayoutPatterns);
  lajkonik::Patterns black_patterns(lajkonik::kExperimentalPlayoutPatterns);

  std::vector<lajkonik::Playout*> playouts[2];
  for (int i = 0; i < NUM_THREADS; ++i) {
    // InitModule in havannah.cc calls srand48().
    playouts[kWhite].push_back(new lajkonik::Playout(
        &playout_options[kWhite],
        &white_patterns,
        mrand48()));
    playouts[kBlack].push_back(new lajkonik::Playout(
        &playout_options[kBlack],
        &black_patterns,
        mrand48()));
  }

  lajkonik::MctsOptions prototype_mcts_options;
  lajkonik::MctsOptions mcts_options[2];

  prototype_mcts_options.exploration_factor = 0.0;
  prototype_mcts_options.rave_bias = 1e-4;
  prototype_mcts_options.first_play_urgency = 1e3;
  prototype_mcts_options.tricky_epsilon = 0.02;
  prototype_mcts_options.locality_bias = 4.0;
  prototype_mcts_options.chain_size_bias_factor = 6.0;
  prototype_mcts_options.rave_update_depth = 1000;
  prototype_mcts_options.expand_after_n_playouts = 160;
  prototype_mcts_options.play_n_playouts_at_once = 1;
  prototype_mcts_options.prior_num_simulations_base = 4;
  prototype_mcts_options.prior_num_simulations_range = 7;
  prototype_mcts_options.prior_reward_halfrange = 5;
  prototype_mcts_options.neighborhood_size = 2;
  prototype_mcts_options.exploration_strategy =
      lajkonik::kSilverWithProgressiveBias;
  prototype_mcts_options.use_rave_randomization = false;
  prototype_mcts_options.use_mate_in_tree = true;
  prototype_mcts_options.use_antimate_in_tree = true;
  prototype_mcts_options.use_deeper_mate_in_tree = true;
  prototype_mcts_options.use_virtual_loss = true;
  prototype_mcts_options.use_solver = true;

  mcts_options[kWhite] = prototype_mcts_options;
  mcts_options[kBlack] = prototype_mcts_options;

  std::vector<lajkonik::MctsEngine*> mcts_engines[2];
  for (int i = 0; i < NUM_THREADS; ++i) {
    mcts_engines[kWhite].push_back(new lajkonik::MctsEngine(
        &mcts_options[kWhite],
        playouts[kWhite][i]));
    mcts_engines[kBlack].push_back(new lajkonik::MctsEngine(
        &mcts_options[kBlack],
        playouts[kBlack][i]));
  }

  lajkonik::ControllerOptions prototype_controller_options;
  lajkonik::ControllerOptions controller_options[2];

  prototype_controller_options.seconds_per_move = 30;
  prototype_controller_options.sole_nonlosing_move_win_ratio_threshold = 0.2;
  prototype_controller_options.win_ratio_threshold = 0.6;
  prototype_controller_options.use_swap = false;
  prototype_controller_options.use_human_like_time_control = false;
  prototype_controller_options.end_games_quickly = false;
  prototype_controller_options.print_debug_info = true;
  prototype_controller_options.clear_tt_after_move = false;  // Don't care.

  controller_options[kWhite] = prototype_controller_options;
  controller_options[kBlack] = prototype_controller_options;

  float o_won = 0.0f;
  for (int i = 0; i < 2500; ++i) {
    float winner;

    winner = ComparePlayers(controller_options, mcts_engines, kWhite);
    o_won += winner;
    printf("o won %.1f/%d times\n", o_won, 2 * i + 1);
    fflush(stdout);

    winner = ComparePlayers(controller_options, mcts_engines, kBlack);
    o_won += winner;
    printf("o won %.1f/%d times\n", o_won, 2 * i + 2);
    fflush(stdout);
  }

  for (int i = 0; i < NUM_THREADS; ++i) {
    delete mcts_engines[kWhite][i];
    delete mcts_engines[kBlack][i];
    delete playouts[kWhite][i];
    delete playouts[kBlack][i];
  }
  return 0;
}
