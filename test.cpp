#include <algorithm>
#include <chrono>
#include <climits> // For INT_MIN and INT_MAX
#include <iostream>
#include <thread>

#include "bot.hpp"

// ---
#define CHESS_NO_EXCEPTIONS
#include <chess.hpp>
// ---

int main() {
  chess::Board board;
  Bot bot;

  int n_turns           = 0;
  auto const start_time = std::chrono::high_resolution_clock::now();
  while (board.isGameOver().second == chess::GameResult::NONE) {
    ++n_turns;

    auto const best_white_move = bot.findBestWhiteMove(board.getFen());

    if (best_white_move.move == chess::Move::NO_MOVE) {
      break;
    }

    board.makeMove(best_white_move.move);
    std::cout << "White plays: " << best_white_move.move
              << "    evaluation: " << evaluateBoard(board)
              << "    side to move: " << board.sideToMove()
              << " best_eval: " << best_white_move.eval << '\n';

    // Check if game is over after White's move
    if (board.isGameOver().second != chess::GameResult::NONE) {
      break;
    }

    // Black's turn (selects the first legal move)
    chess::Movelist black_moves;
    chess::movegen::legalmoves(black_moves, board);

    if (black_moves.empty()) {
      break; // No legal moves, game over
    }

    // Make the first move for Black
    board.makeMove(black_moves[0]);
    std::cout << "Black plays: " << black_moves[0] << "    evaluation: " << evaluateBoard(board)
              << "    side to move: " << board.sideToMove() << '\n';
  }

  auto const end_time = std::chrono::high_resolution_clock::now();
  auto const duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

  // Display game result
  auto const game_over = board.isGameOver();
  std::cout << "Game over in " << n_turns << ". Took " << duration.count() << " seconds. "
            << "Result: ";
  if (game_over.second == chess::GameResult::LOSE) {
    // TODO: Make sure it doesn't return NONE color
    auto const side_to_move = board.sideToMove();
    if (side_to_move == chess::Color::WHITE) {
      std::cout << "Black wins.\n";
    } else {
      std::cout << "White wins.\n";
    }
  } else {
    std::cout << "Draw.\n";
  }

  return 0;
}
