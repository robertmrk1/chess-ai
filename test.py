import subprocess
import threading
import chess
import chess.svg
from PyQt5.QtSvg import QSvgWidget
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QLineEdit, QPushButton, QMessageBox
from PyQt5.QtCore import Qt, pyqtSignal, QObject, QThread


class ChessBot:
    def __init__(self, bot_path):
        """
        Initialize the ChessBot class with the path to the C++ executable.
        """
        self.process = subprocess.Popen(
            bot_path,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True
        )

    def send_fen(self, fen):
        """
        Send a FEN string to the bot and receive the best move.
        """
        if self.process.poll() is not None:
            raise RuntimeError("Bot process has terminated unexpectedly")

        # Send the FEN string followed by a newline
        self.process.stdin.write(fen + "\n")
        self.process.stdin.flush()

        # Read and return the bot's response
        return self.process.stdout.readline().strip()

    def quit(self):
        """
        Tell the bot to quit and close the process.
        """
        if self.process.poll() is None:
            self.process.stdin.write("quit\n")
            self.process.stdin.flush()
            self.process.stdin.close()
            self.process.wait()


class BotWorker(QObject):
    finished = pyqtSignal(str)

    def __init__(self, fen, bot):
        super().__init__()
        self.fen = fen
        self.bot = bot

    def run(self):
        best_move = self.bot.send_fen(self.fen)
        self.finished.emit(best_move)


class ChessBoardWidget(QSvgWidget):
    squareClicked = pyqtSignal(str)  # Signal to emit when a move is made

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)
        self.start_square = None
        self.end_square = None

    def mousePressEvent(self, event):
        x = event.x()
        y = event.y()
        square = self.get_square_from_coords(x, y)
        if square:
            self.start_square = square
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        x = event.x()
        y = event.y()
        square = self.get_square_from_coords(x, y)
        if square and self.start_square:
            self.end_square = square
            move = self.start_square + self.end_square
            self.squareClicked.emit(move)
        self.start_square = None
        self.end_square = None
        super().mouseReleaseEvent(event)

    def get_square_from_coords(self, x, y):
        square_size = self.width() / 8
        file_index = int(x / square_size)
        rank_index = int(y / square_size)

        # Adjust for flipped board
        file_number = 7 - file_index
        rank_number = rank_index

        if 0 <= file_number <= 7 and 0 <= rank_number <= 7:
            square_index = chess.square(file_number, rank_number)
            square = chess.square_name(square_index)
            return square
        return None


class ChessGUI(QWidget):
    def __init__(self, bot_path):
        super().__init__()
        self.setWindowTitle("Chess GUI - Play as Black")
        self.setGeometry(100, 100, 600, 700)

        self.bot = ChessBot(bot_path)
        self.board = chess.Board()

        # Layout for widgets
        self.layout = QVBoxLayout(self)

        # SVG Widget for chessboard
        self.svg_widget = ChessBoardWidget(self)
        self.svg_widget.setFixedSize(600, 600)
        self.layout.addWidget(self.svg_widget)

        # Input for UCI moves (optional)
        self.move_input = QLineEdit(self)
        self.move_input.setPlaceholderText(
            "Enter your move in UCI format (e.g., e7e5)")
        self.layout.addWidget(self.move_input)

        # Button to submit the move (optional)
        self.submit_button = QPushButton("Submit Move", self)
        self.submit_button.clicked.connect(self.handle_user_move)
        self.layout.addWidget(self.submit_button)

        # Connect the squareClicked signal to process_move
        self.svg_widget.squareClicked.connect(self.process_move)

        # Render the initial board
        self.update_board()

        # AI makes the first move
        self.bot_move()

    def update_board(self):
        """Update the chessboard display."""
        svg_data = chess.svg.board(self.board, flipped=True).encode(
            "utf-8")  # Flipped for Black
        self.svg_widget.load(svg_data)

    def set_input_enabled(self, enabled):
        """Enable or disable the input box and button."""
        self.move_input.setEnabled(enabled)
        self.submit_button.setEnabled(enabled)
        self.svg_widget.setEnabled(enabled)

    def handle_user_move(self):
        """Handle user input from the text box and update the board."""
        user_move = self.move_input.text().strip()
        self.process_move(user_move)

    def process_move(self, uci_move):
        """Process a move made via drag-and-drop or text input."""
        if not uci_move:
            QMessageBox.warning(self, "Invalid Input",
                                "Please enter a valid move.")
            return

        try:
            # Apply the user's move
            move = chess.Move.from_uci(uci_move)
            if move in self.board.legal_moves:
                self.board.push(move)
                self.update_board()
                self.move_input.clear()

                # Disable input while the bot is thinking
                self.set_input_enabled(False)

                # Bot's turn
                self.bot_move()
            else:
                QMessageBox.warning(self, "Invalid Move",
                                    f"The move '{uci_move}' is not legal.")
        except ValueError:
            QMessageBox.warning(
                self, "Invalid Input", "Please enter a move in UCI format (e.g., e7e5).")

    def bot_move(self):
        """Get the bot's move and update the board."""
        if not self.board.is_game_over():
            # Disable user input while AI is thinking
            self.set_input_enabled(False)
            # Create a QThread
            self.thread = QThread()
            # Create a worker object
            self.worker = BotWorker(self.board.fen(), self.bot)
            # Move worker to the thread
            self.worker.moveToThread(self.thread)
            # Connect signals and slots
            self.thread.started.connect(self.worker.run)
            self.worker.finished.connect(self.on_bot_move_finished)
            self.worker.finished.connect(self.thread.quit)
            self.worker.finished.connect(self.worker.deleteLater)
            self.thread.finished.connect(self.thread.deleteLater)
            # Start the thread
            self.thread.start()
        else:
            self.display_game_over()

    def on_bot_move_finished(self, best_move):
        """Handle the bot's move after it finishes computing."""
        self.board.push_uci(best_move)
        self.update_board()
        self.set_input_enabled(True)
        if self.board.is_game_over():
            self.display_game_over()

    def display_game_over(self):
        """Show a message when the game ends."""
        if self.board.is_checkmate():
            winner = "White" if self.board.turn == chess.BLACK else "Black"
            QMessageBox.information(
                self, "Game Over", f"Checkmate! {winner} wins.")
        elif self.board.is_stalemate():
            QMessageBox.information(self, "Game Over", "Stalemate!")
        elif self.board.is_insufficient_material():
            QMessageBox.information(
                self, "Game Over", "Draw due to insufficient material.")
        else:
            QMessageBox.information(self, "Game Over", "Game over!")
        # Disable further input after game over
        self.set_input_enabled(False)

    def closeEvent(self, event):
        """Handle window close event to terminate the bot."""
        self.bot.quit()
        event.accept()


if __name__ == "__main__":
    import sys

    bot_path = "C:/Users/rober/Documents/code/chess_ai/build/Release/chess_ai.exe"
    app = QApplication(sys.argv)
    chess_gui = ChessGUI(bot_path)
    chess_gui.show()
    sys.exit(app.exec_())
