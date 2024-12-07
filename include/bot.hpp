#pragma once
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
constexpr auto SEARCH_DEPTH = 8;

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

// MVV_LVA_LUT[attacker][victim]
constexpr std::array<std::array<int, 7>, 7> MVV_LVA_LUT{{
    {{15, 25, 35, 45, 55, 0, 0}}, // PAWN
    {{14, 24, 34, 44, 54, 0, 0}}, // KNIGHT
    {{13, 23, 33, 43, 53, 0, 0}}, // BISHOP
    {{12, 22, 32, 42, 52, 0, 0}}, // ROOK
    {{11, 21, 31, 41, 51, 0, 0}}, // QUEEN
    {{10, 20, 30, 40, 50, 0, 0}}, // KING
    {{0, 0, 0, 0, 0, 0, 0}}       // NONE (EMPTY)
}};

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
      return {chess::Move::NO_MOVE};
    }

    // Evaluate once at the root
    int const root_eval = evaluateBoard(original_board);

    std::vector<int> move_evals(moves.size());
    std::vector<std::thread> threads;

    auto evaluate_moves = [&](chess::Board const &original_board, chess::Movelist const &moves,
                              int const start_index, int const end_index, int root_eval) {
      auto board = original_board;
      for (int i = start_index; i < end_index; ++i) {
        // Incremental evaluation on this move
        int current_eval = root_eval;
        // Apply incremental changes for this move
        applyIncrementalEval(board, moves[i], current_eval);
        board.makeMove(moves[i]);
        move_evals[i] = minimax(board, SEARCH_DEPTH - 1, std::numeric_limits<int>::min(),
                                std::numeric_limits<int>::max(), false, current_eval);
        board.unmakeMove(moves[i]);
      }
    };

    auto const num_threads      = std::min(NTHREADS, static_cast<int>(moves.size()));
    auto const moves_per_thread = (moves.size() + num_threads - 1) / num_threads;
    for (int i = 0; i < num_threads; ++i) {
      auto const start_index = i * moves_per_thread;
      auto const end_index =
          std::min(start_index + moves_per_thread, static_cast<int>(moves.size()));

      threads.emplace_back(evaluate_moves, original_board, moves, start_index, end_index,
                           root_eval);
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
      move_eval_list.push_back({moves[static_cast<int>(i)], move_evals[i]});
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

    // Filter out non-capturing moves if we have capturing moves
    std::vector<chess::Move> aggressive_moves;
    for (auto const &move : max_eval_moves) {
      if (original_board.at(move.to()) != chess::Piece::NONE) {
        // This move captures a piece
        aggressive_moves.push_back(move);
      }
    }

    std::vector<chess::Move> candidate_moves;
    if (not aggressive_moves.empty()) {
      candidate_moves = aggressive_moves;
    } else {
      candidate_moves = max_eval_moves;
    }

    // Pick a move randomly among the candidate moves
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distribution(0, static_cast<int>(candidate_moves.size()) - 1);
    chess::Move const selected_move = candidate_moves[distribution(gen)];

    return {selected_move, max_eval};
  }

  std::string findBestWhiteMoveUci(std::string fen) {
    return chess::uci::moveToUci(findBestWhiteMove(fen).move);
  }

private:
  // This function applies the incremental evaluation changes without making the move yet.
  void applyIncrementalEval(chess::Board const &board, chess::Move const &move,
                            int &current_eval) const {
    // If capture, remove victim's value
    auto victim = board.at(move.to());
    if (victim != chess::Piece::NONE) {
      current_eval -= pieceValue(victim); // Remove victim from evaluation
    }

    if (move.typeOf() == chess::Move::ENPASSANT) {
      if (board.sideToMove() == chess::Color::WHITE) {
        current_eval -= pieceValue(chess::Piece::BLACKPAWN);
      } else {
        current_eval -= pieceValue(chess::Piece::WHITEPAWN);
      }
    } else if (move.typeOf() == chess::Move::PROMOTION) {
      auto const attacker = board.at(move.from());
      // Remove pawn value
      current_eval -= pieceValue(attacker);
      // Add promoted piece value
      chess::Piece promoted_piece;
      switch (move.promotionType().internal()) {
      case chess::PieceType::QUEEN:
        promoted_piece = board.sideToMove() == chess::Color::WHITE ? chess::Piece::WHITEQUEEN
                                                                   : chess::Piece::BLACKQUEEN;
        break;
      case chess::PieceType::ROOK:
        promoted_piece = board.sideToMove() == chess::Color::WHITE ? chess::Piece::WHITEROOK
                                                                   : chess::Piece::BLACKROOK;
        break;
      case chess::PieceType::BISHOP:
        promoted_piece = board.sideToMove() == chess::Color::WHITE ? chess::Piece::WHITEBISHOP
                                                                   : chess::Piece::BLACKBISHOP;
        break;
      case chess::PieceType::KNIGHT:
        promoted_piece = board.sideToMove() == chess::Color::WHITE ? chess::Piece::WHITEKNIGHT
                                                                   : chess::Piece::BLACKKNIGHT;
        break;
      default:
        promoted_piece = chess::Piece::NONE;
        break;
      }

      current_eval += pieceValue(promoted_piece);
    }
    // If there's no capture or promotion or enpassant, evaluation doesn't change.
  }

  [[nodiscard]] int minimax(chess::Board &board, int depth, int alpha, int beta,
                            bool maximizing_player, int current_eval) {
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
      return current_eval;
    }

    orderMoves(movelist, board);

    int best_score;
    if (maximizing_player) {
      best_score = std::numeric_limits<int>::min();
      for (auto const &move : movelist) {
        int old_eval = current_eval;
        applyIncrementalEval(board, move, current_eval);
        board.makeMove(move);

        auto const eval = minimax(board, depth - 1, alpha, beta, false, current_eval);

        board.unmakeMove(move);
        current_eval = old_eval; // restore evaluation

        best_score = std::max(best_score, eval);
        alpha      = std::max(alpha, eval);
        if (beta <= alpha) {
          break; // Beta cut-off
        }
      }
    } else {
      best_score = std::numeric_limits<int>::max();
      for (auto const &move : movelist) {
        int old_eval = current_eval;
        applyIncrementalEval(board, move, current_eval);
        board.makeMove(move);

        auto const eval = minimax(board, depth - 1, alpha, beta, true, current_eval);

        board.unmakeMove(move);
        current_eval = old_eval; // restore evaluation

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
