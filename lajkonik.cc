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

// The main module for playing Havannah.

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string>
#include <utility>
#include <vector>
#include "readline/history.h"
#include "readline/readline.h"

#include "base.h"
#include "controller.h"
#include "define-playout-patterns.h"
#include "frontend.h"
#include "havannah.h"
#include "mcts.h"
#include "mongoose.h"
#include "playout.h"

namespace lajkonik {

class StdioFrontend : public Frontend {
 public:
  StdioFrontend(
      Controller* controller,
      Player* player,
      int* result,
      volatile bool* is_thinking)
      : Frontend(controller, player, result, is_thinking) {}
  ~StdioFrontend() {}

 private:
  virtual void VPrintf(const char* format, va_list ap) const {
    vprintf(format, ap);
  }
  virtual void Flush() const {
    fflush(stdout);
  }
 
  StdioFrontend(const StdioFrontend&);
  void operator=(const StdioFrontend&);
};

class HttpFrontend : public Frontend {
 public:
  HttpFrontend(
      Controller* controller,
      Player* player,
      int* result,
      volatile bool* is_thinking)
      : Frontend(controller, player, result, is_thinking),
        connection_(NULL) {}
  ~HttpFrontend() {}

  void HandleRequest(mg_connection* connection) {
    const mg_request_info* request_info = mg_get_request_info(connection);
    if (strcmp(request_info->uri, "/") == 0) {
      mg_printf(connection, "%s", kHtmlResponse);
      mg_printf(connection, kMainPageTemplate, SIDE_LENGTH);
    } else if (strcmp(request_info->uri, "/status.html") == 0) {
      mg_printf(connection, "%s", kHtmlResponse);
      mg_printf(connection, kStatusPageTemplate, SIDE_LENGTH, SIDE_LENGTH);
    } else if (strcmp(request_info->uri, "/havannah.js") == 0) {
      mg_send_file(connection, "http/havannah.js");
    } else if (strcmp(request_info->uri, "/exec") == 0) {
      int query_string_len = strlen(request_info->query_string);
      char* command = new char[query_string_len];
      if (mg_get_var(
          request_info->query_string, query_string_len,
          "cmd", command, query_string_len) >= 0) {
        InteractWithJavaScript(command, connection);
      } else if (mg_get_var(
          request_info->query_string, query_string_len,
          "status", command, query_string_len) >= 0) {
        char player;
        sscanf(command, "status=%c", &player);
        std::string first_status;
        std::string second_status;
        get_controller()->GetStatus(&first_status, &second_status);
        mg_printf(connection, "%s", kJavaScriptResponse);
        mg_printf(
            connection,
            "drawStatus([{id:'first',player:%d,status:'%s'},\n",
            player != 'w', first_status.c_str());
        mg_printf(
            connection,
            "{id:'second',player:%d,status:'%s'}])",
            player == 'w', second_status.c_str());
      }
      delete[] command;
    }
  }

 private:
  virtual void VPrintf(const char* format, va_list ap) const {
    std::string s = StringVPrintf(format, ap);
    assert(connection_ != NULL);
    mg_write(connection_, s.c_str(), s.size());
  }
  virtual void Flush() const {}

  void InteractWithJavaScript(char* command, mg_connection* connection) {
    if (strcmp(command, "name") == 0) {
      std::string board = get_controller()->GetBoard();
      mg_printf(connection, "drawBoard('game',0,1,'%s')", board.c_str());
      return;
    }
    const std::string command_backup(command);
    connection_ = connection;
    std::vector<char*> args = HandleCommand(command);
    if (strcmp(command, "play") != 0 && strcmp(command, "genmove") != 0) {
      return;
    }
    if (get_is_thinking()) {
      mg_printf(connection, "badCommand('Please wait for your turn')");
      return;
    }
    mg_printf(connection, "%s", kJavaScriptResponse);
    if (args.size() > 1 && command_succeeded()) {
      const std::string last_move =
          ToClassicalString(get_controller()->position().MoveNPliesAgo(0));
      mg_printf(
          connection, "drawStone('game',%d,'%s',%d)",
          args[0][0] != 'w', last_move.c_str(), get_result());
      return;
    }
    mg_printf(
        connection, "badCommand('Bad command: %s')",
        command_backup.c_str());
  }
  
  static const char kHtmlResponse[];
  static const char kMainPageTemplate[];
  static const char kStatusPageTemplate[];
  static const char kJavaScriptResponse[];
  
  mg_connection* connection_;
 
  HttpFrontend(const HttpFrontend&);
  void operator=(const HttpFrontend&);
};

const char HttpFrontend::kHtmlResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n\r\n";
const char HttpFrontend::kMainPageTemplate[] =
    "<html>\n<head>\n"
    "<script type=\"text/javascript\" src=\"havannah.js\"></script>\n"
    "</head>\n<body onload=\"initBoard('game',%d,0,1)\">\n"
    "<canvas id=\"game\"></canvas>\n</body>\n</html>";
const char HttpFrontend::kStatusPageTemplate[] =
    "<html>\n<head>\n"
    "<script type=\"text/javascript\" src=\"havannah.js\"></script>\n"
    "</head>\n<body onload="
    "\"initBoard('first',%d,0,0);initBoard('second',%d,0,0);getStatus()\">\n"
    "<canvas id=\"first\"></canvas>\n<canvas id=\"second\"></canvas>\n"
    "</body>\n</html>";
const char HttpFrontend::kJavaScriptResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/javascript\r\n"
    "Cache-Control: no-store\r\n\r\n";

namespace {

HttpFrontend* g_http_frontend;

#if 0

void SendResponse(mg_connection* connection,
                  Frontend* frontend,
                  char* command) {
  if (frontend->is_thinking()) {
    mg_printf(connection, "badCommand('Please wait for your turn')");
  } else {
    char player;
    sscanf(command, "%*s %c", &player);
    std::string command_backup(command);
    frontend->HandleCommand(command);
    mg_printf(connection, kJavaScriptResponse);
    if (frontend->command_succeeded()) {
      std::string last_move = ToClassicalString(frontend->last_move());
      mg_printf(connection, "drawStone('game',%d,'%s',%d)",
                player != 'w', last_move.c_str(), frontend->result());
    } else {
      mg_printf(connection, "badCommand('Bad command: %s')",
                command_backup.c_str());
    }
  }
}

#endif

void* EventHandler(mg_event event, mg_connection* connection) {
  if (event != MG_NEW_REQUEST)
    return NULL;
  g_http_frontend->HandleRequest(connection);
  return const_cast<char*>("yes");
}

char** LajkonikCompletion(const char* text, int start, int /*end*/) {
  rl_attempted_completion_over = 1;
  if (start == 0)
    return rl_completion_matches(text, Frontend::CommandGenerator);
  else
    return NULL;
}

}  // namespace
}  // namespace lajkonik

namespace {

char* GetLine() {
  static char buffer[1024];
  if (isatty(fileno(stdout)))
    return readline(NULL);
  else
    return fgets(buffer, sizeof buffer, stdin);
}

}  // namespace

int main() {
  lajkonik::PlayoutOptions playout_options;

  playout_options.initial_chance_of_ring_notice = 150.0;
  playout_options.final_chance_of_ring_notice = -350.0;
  playout_options.chance_of_forced_connection_intercept = 34.0;
  playout_options.chance_of_forced_connection_slope = -30.0;
  playout_options.chance_of_connection_defense_intercept = 42.0;
  playout_options.chance_of_connection_defense_slope = -28.0;
  playout_options.retries_of_isolated_moves = 1;
  playout_options.use_havannah_mate = true;
  playout_options.use_havannah_antimate = true;
  playout_options.use_ring_detection = true;

  lajkonik::Patterns patterns(lajkonik::kPlayoutPatterns);

  std::vector<lajkonik::Playout*> playouts;
  for (int i = 0; i < NUM_THREADS; ++i) {
    // InitModule in havannah.cc calls srand48().
    playouts.push_back(new lajkonik::Playout(
        &playout_options,
        &patterns,
        mrand48()));
  }

  lajkonik::MctsOptions mcts_options;
  mcts_options.exploration_factor = 0.0;
  mcts_options.rave_bias = 1e-4;
  mcts_options.first_play_urgency = 1e3;
  mcts_options.tricky_epsilon = 0.02;
  mcts_options.locality_bias = 1.0;
  mcts_options.chain_size_bias_factor = 0.0;
  mcts_options.rave_update_depth = 1000;
  mcts_options.expand_after_n_playouts = 160;
  mcts_options.play_n_playouts_at_once = 1;
  mcts_options.prior_num_simulations_base = 4;
  mcts_options.prior_num_simulations_range = 7;
  mcts_options.prior_reward_halfrange = 5;
  mcts_options.neighborhood_size = 2;
  mcts_options.exploration_strategy = lajkonik::kSilverWithProgressiveBias;
  mcts_options.use_rave_randomization = false;
  mcts_options.use_mate_in_tree = true;
  mcts_options.use_antimate_in_tree = true;
  mcts_options.use_deeper_mate_in_tree = true;
  mcts_options.use_virtual_loss = true;
  mcts_options.use_solver = true;

  std::vector<lajkonik::MctsEngine*> mcts_engines;
  for (int i = 0; i < NUM_THREADS; ++i) {
    mcts_engines.push_back(new lajkonik::MctsEngine(
        &mcts_options,
        playouts[i]));
  }

  lajkonik::ControllerOptions controller_options;
  controller_options.seconds_per_move = 30;  // 40 * lajkonik::SIDE_LENGTH;
  controller_options.sole_nonlosing_move_win_ratio_threshold = 0.2;
  controller_options.win_ratio_threshold = 0.6;
  controller_options.use_swap = false;
  controller_options.use_human_like_time_control = false;
  controller_options.end_games_quickly = false;
  controller_options.print_debug_info = true;
  controller_options.clear_tt_after_move = false;  // TODO: change.
  lajkonik::Controller controller(controller_options, mcts_engines);

  lajkonik::Player player = lajkonik::kWhite;
  int result = lajkonik::kNoneWon;
  volatile bool is_thinking = false;
  lajkonik::StdioFrontend stdio_frontend(
      &controller, &player, &result, &is_thinking);
  lajkonik::g_http_frontend = new lajkonik::HttpFrontend(
      &controller, &player, &result, &is_thinking);
  mg_start(lajkonik::EventHandler, NULL, NULL);
  rl_attempted_completion_function = lajkonik::LajkonikCompletion;
  char* command;
  while ((command = GetLine()) != NULL) {
    if (command[0] != '\0')
      add_history(command);
    stdio_frontend.HandleCommand(command);
  }
  for (int i = 0; i < NUM_THREADS; ++i) {
    delete mcts_engines[i];
    delete playouts[i];
  }
}
