"""Main entry point for the Keithley Data Collector application."""

import sys
from PyQt5.QtWidgets import QApplication, QSystemTrayIcon, QMessageBox

from ui.main_window import MainWindow


def main():
    """Run the application."""
    # Create application
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)  # Keep running when window closed
    
    # Check system tray availability
    if not QSystemTrayIcon.isSystemTrayAvailable():
        QMessageBox.critical(None, "System Tray",
                           "System tray is not available on this system.")
    
    # Create and show main window
    window = MainWindow()
    window.show()
    
    # Run application
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()