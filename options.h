#ifndef OPTIONS_H_
#define OPTIONS_H_

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

// The definition of structs storing options for various program components.

#include <string>

#include "base.h"

namespace lajkonik {

static const char* FormatString(int) { return "%d"; }
static const char* FormatString(float) { return "%f"; }

#define ADD_STRING(field) (result += StringPrintf(StringPrintf( \
    "%s.%s = %s;\n", struct_name, #field, FormatString(field)).c_str(), field))

struct PlayoutOptions {
  float initial_chance_of_ring_notice;
  float final_chance_of_ring_notice;
  float chance_of_forced_connection_slope;
  float chance_of_forced_connection_intercept;
  float chance_of_connection_defense_slope;
  float chance_of_connection_defense_intercept;
  int retries_of_isolated_moves;
  bool use_havannah_mate;
  bool use_havannah_antimate;
  bool use_ring_detection;

  std::string ToString() const {
    const char struct_name[] = "playout_options";
    std::string result;
    ADD_STRING(initial_chance_of_ring_notice);
    ADD_STRING(final_chance_of_ring_notice);
    ADD_STRING(chance_of_forced_connection_slope);
    ADD_STRING(chance_of_forced_connection_intercept);
    ADD_STRING(chance_of_connection_defense_slope);
    ADD_STRING(chance_of_connection_defense_intercept);
    ADD_STRING(use_havannah_mate);
    ADD_STRING(use_havannah_antimate);
    ADD_STRING(use_ring_detection);
    return result;
  }
};

struct MctsOptions {
  float exploration_factor;
  float rave_bias;
  float first_play_urgency;
  float tricky_epsilon;
  float locality_bias;
  float chain_size_bias_factor;
  int expand_after_n_playouts;
  int play_n_playouts_at_once;
  int exploration_strategy;
  int rave_update_depth;
  int prior_num_simulations_base;
  int prior_num_simulations_range;
  int prior_reward_halfrange;
  int neighborhood_size;
  bool use_rave_randomization;
  bool use_mate_in_tree;
  bool use_antimate_in_tree;
  bool use_deeper_mate_in_tree;
  bool use_virtual_loss;
  bool use_solver;

  std::string ToString() const {
    const char struct_name[] = "mcts_options";
    std::string result;
    ADD_STRING(exploration_factor);
    ADD_STRING(rave_bias);
    ADD_STRING(tricky_epsilon);
    ADD_STRING(locality_bias);
    ADD_STRING(chain_size_bias_factor);
    ADD_STRING(first_play_urgency);
    ADD_STRING(expand_after_n_playouts);
    ADD_STRING(play_n_playouts_at_once);
    ADD_STRING(exploration_strategy);
    ADD_STRING(rave_update_depth);
    ADD_STRING(prior_num_simulations_base);
    ADD_STRING(prior_num_simulations_range);
    ADD_STRING(prior_reward_halfrange);
    ADD_STRING(neighborhood_size);
    ADD_STRING(use_rave_randomization);
    ADD_STRING(use_mate_in_tree);
    ADD_STRING(use_antimate_in_tree);
    ADD_STRING(use_deeper_mate_in_tree);
    ADD_STRING(use_virtual_loss);
    ADD_STRING(use_solver);
    return result;
  }
};

struct ControllerOptions {
  float sole_nonlosing_move_win_ratio_threshold;
  float win_ratio_threshold;
  int seconds_per_move;
  bool end_games_quickly;
  bool print_debug_info;
  bool use_human_like_time_control;
  bool use_swap;
  bool clear_tt_after_move;

  std::string ToString() const {
    const char struct_name[] = "controller_options";
    std::string result;
    ADD_STRING(seconds_per_move);
    ADD_STRING(use_swap);
    ADD_STRING(use_human_like_time_control);
    return result;
  }
};

#undef ADD_STRING

}  // namespace lajkonik

#endif  // OPTIONS_H_
