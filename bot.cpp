#include "bot.hpp"

int main() {
  Bot bot;

  std::string input;
  while (true) {
    // Read a line of input
    std::getline(std::cin, input);

    // Exit condition
    if (input == "quit") {
      break;
    }

    // Echo the input back
    std::cout << bot.findBestWhiteMoveUci(input) << std::endl;
  }
  return 0;
}
