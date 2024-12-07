#include <algorithm>
#include <chrono>
#include <climits>
#include <iostream>
#include <random>
#include <thread>

// ---
#define CHESS_NO_EXCEPTIONS
#include <chess.hpp>
// ---

constexpr auto MATE_SCORE   = std::numeric_limits<int>::max() / 2;
constexpr auto NTHREADS     = 32;
constexpr auto SEARCH_DEPTH = 7;

inline constexpr int pieceValue(chess::Piece const &piece) {
  switch (piece.internal()) {
  case chess::Piece::underlying::WHITEPAWN:
    return 100;
  case chess::Piece::underlying::BLACKPAWN:
    return -100;
  case chess::Piece::underlying::WHITEKNIGHT:
    return 320;
  case chess::Piece::underlying::BLACKKNIGHT:
    return -320;
  case chess::Piece::underlying::WHITEBISHOP:
    return 330;
  case chess::Piece::underlying::BLACKBISHOP:
    return -330;
  case chess::Piece::underlying::WHITEROOK:
    return 500;
  case chess::Piece::underlying::BLACKROOK:
    return -500;
  case chess::Piece::underlying::WHITEQUEEN:
    return 900;
  case chess::Piece::underlying::BLACKQUEEN:
    return -900;
  default:
    return 0;
  }
}

// MVV_LVA_LUT[attacker][victim] Most Valuable Victim, Least Valuable Attacker
constexpr std::array<std::array<int, 7>, 7> MVV_LVA_LUT{{
    {{15, 25, 35, 45, 55, 0, 0}}, // PAWN
    {{14, 24, 34, 44, 54, 0, 0}}, // KNIGHT
    {{13, 23, 33, 43, 53, 0, 0}}, // BISHOP
    {{12, 22, 32, 42, 52, 0, 0}}, // ROOK
    {{11, 21, 31, 41, 51, 0, 0}}, // QUEEN
    {{10, 20, 30, 40, 50, 0, 0}}, // KING
    {{0, 0, 0, 0, 0, 0, 0}}       // NONE (EMPTY)
}};

// Function to evaluate the board based on piece values
[[nodiscard]] inline int evaluateBoard(chess::Board const &board) {
  int score = 0;
  for (chess::Square square(0); square < chess::Square::underlying::NO_SQ; ++square) {
    auto const piece = board.at(square);
    score += pieceValue(piece);
  }
  return score;
}

[[nodiscard]] inline int moveHeuristic(chess::Move const &move, chess::Board const &board) {
  // MVV-LVA Most Valuable Victim, Least Valuable Attacker
  auto const attacker_type = board.at(move.from()).type();
  auto const victim_type   = board.at(move.to()).type();

  return MVV_LVA_LUT[attacker_type][victim_type];
}

inline void orderMoves(chess::Movelist &moves, chess::Board const &board) {
  std::sort(moves.begin(), moves.end(), [&board](chess::Move const &a, chess::Move const &b) {
    auto const score_a = moveHeuristic(a, board);
    auto const score_b = moveHeuristic(b, board);
    return score_a > score_b;
  });
}

struct MoveWithEval {
  chess::Move move;
  int eval;
};

struct Bot {

  struct MoveAndEval {
    chess::Move move;
    int eval;
  };

  [[nodiscard]] MoveAndEval findBestWhiteMove(std::string fen) {
    chess::Board const original_board(fen);

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, original_board);

    if (moves.empty()) {
      return {};
    }

    std::vector<int> move_evals(moves.size());
    std::vector<std::thread> threads;

    auto evaluate_moves = [&](chess::Board const &original_board, chess::Movelist const &moves,
                              int const start_index, int const end_index) {
      auto board = original_board;
      for (int i = start_index; i < end_index; ++i) {
        board.makeMove(moves[i]);
        move_evals[i] = minimax(board, SEARCH_DEPTH, INT_MIN, INT_MAX, false);
        board.unmakeMove(moves[i]);
      }
    };

    auto const num_threads      = std::min(NTHREADS, static_cast<int>(moves.size()));
    auto const moves_per_thread = (moves.size() + num_threads - 1) / num_threads;
    for (int i = 0; i < num_threads; ++i) {
      auto const start_index = i * moves_per_thread;
      auto const end_index =
          std::min(start_index + moves_per_thread, static_cast<int>(moves.size()));

      threads.emplace_back(evaluate_moves, original_board, moves, start_index, end_index);
    }

    for (auto &thread : threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    // Collect moves and their evaluations
    struct MoveEval {
      chess::Move move;
      int eval;
    };
    std::vector<MoveEval> move_eval_list;
    for (size_t i = 0; i < moves.size(); ++i) {
      move_eval_list.push_back({moves[i], move_evals[i]});
    }

    // Sort moves in increasing evaluation order
    std::sort(move_eval_list.begin(), move_eval_list.end(),
              [](MoveEval const &a, MoveEval const &b) { return a.eval < b.eval; });

    // Find the maximum evaluation
    int const max_eval = move_eval_list.back().eval;

    // Collect all moves with the maximum evaluation
    std::vector<chess::Move> max_eval_moves;
    for (auto const &me : move_eval_list) {
      if (me.eval == max_eval) {
        max_eval_moves.push_back(me.move);
      }
    }

    // Filter out "kind moves" (non-capturing moves) if possible
    std::vector<chess::Move> aggressive_moves;
    for (auto const &move : max_eval_moves) {
      if (original_board.at(move.to()) != chess::Piece::NONE) {
        // This move captures a piece
        aggressive_moves.push_back(move);
      }
    }

    // Choose candidate moves based on the presence of aggressive moves
    std::vector<chess::Move> candidate_moves;
    if (not aggressive_moves.empty()) {
      candidate_moves = aggressive_moves;
    } else {
      candidate_moves = max_eval_moves;
    }

    // Pick a move randomly among the candidate moves
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distribution(0, candidate_moves.size() - 1);
    chess::Move selected_move = candidate_moves[distribution(gen)];

    return {selected_move, max_eval};
  }

  std::string findBestWhiteMoveUci(std::string fen) {
    return chess::uci::moveToUci(findBestWhiteMove(fen).move);
  }

private:
  [[nodiscard]] int minimax(chess::Board &board, int depth, int alpha, int beta,
                            bool maximizing_player) {
    chess::Movelist movelist;
    chess::movegen::legalmoves(movelist, board);

    auto const game_over = board.isGameOver(movelist);
    if (game_over.second != chess::GameResult::NONE) {
      if (game_over.second == chess::GameResult::LOSE) {
        return maximizing_player ? -MATE_SCORE - depth : MATE_SCORE + depth;
      }
      return 0;
    }

    if (depth == 0) {
      return evaluateBoard(board);
    }

    orderMoves(movelist, board);

    int best_score;
    if (maximizing_player) {
      best_score = std::numeric_limits<int>::min();
      for (auto const &move : movelist) {
        board.makeMove(move);
        auto const eval = minimax(board, depth - 1, alpha, beta, false);
        board.unmakeMove(move);
        best_score = std::max(best_score, eval);
        alpha      = std::max(alpha, eval);
        if (beta <= alpha) {
          break; // Beta cut-off
        }
      }
    } else {
      best_score = std::numeric_limits<int>::max();
      for (auto const &move : movelist) {
        board.makeMove(move);
        auto const eval = minimax(board, depth - 1, alpha, beta, true);
        board.unmakeMove(move);
        best_score = std::min(best_score, eval);
        beta       = std::min(beta, eval);
        if (beta <= alpha) {
          break; // Alpha cut-off
        }
      }
    }

    return best_score;
  }
};
