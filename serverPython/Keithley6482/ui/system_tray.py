"""System tray icon module for background operation."""

from PyQt5.QtWidgets import QSystemTrayIcon, QMenu, QAction, QMessageBox
from PyQt5.QtGui import QIcon, QPixmap, QPainter, QBrush, QPen, QFont, QColor
from PyQt5.QtCore import Qt


class SystemTrayIcon(QSystemTrayIcon):
    """System tray icon for the application."""
    
    def __init__(self, parent=None):
        """
        Initialize the system tray icon.
        
        Args:
            parent: Parent window
        """
        super().__init__(parent)
        self.parent_window = parent
        self._setup_icon()
        self._setup_menu()
    
    def _create_icon(self, color: str = 'green') -> QIcon:
        """
        Create a colored circular icon.
        
        Args:
            color: Icon color ('green' or 'red')
            
        Returns:
            QIcon instance
        """
        pixmap = QPixmap(64, 64)
        pixmap.fill(Qt.transparent)
        
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.Antialiasing)
        
        # Set color based on status
        if color == 'green':
            brush = QBrush(QColor(0, 255, 0))
            pen = QPen(QColor(0, 204, 0), 2)
        else:
            brush = QBrush(QColor(255, 0, 0))
            pen = QPen(QColor(204, 0, 0), 2)
        
        painter.setBrush(brush)
        painter.setPen(pen)
        painter.drawEllipse(8, 8, 48, 48)
        
        # Add "D" for Data Collector
        painter.setPen(QPen(QColor(255, 255, 255), 2))
        painter.setFont(QFont('Arial', 20, QFont.Bold))
        painter.drawText(20, 38, 'D')
        
        painter.end()
        return QIcon(pixmap)
    
    def _setup_icon(self):
        """Setup the initial icon."""
        self.setIcon(self._create_icon('green'))
        self.setToolTip('Data Collector - Running')
    
    def _setup_menu(self):
        """Setup the context menu."""
        menu = QMenu()
        
        # Show/Hide actions
        show_action = QAction('Show Window', self)
        show_action.triggered.connect(self.show_window)
        menu.addAction(show_action)
        
        hide_action = QAction('Hide to Tray', self)
        hide_action.triggered.connect(self.hide_window)
        menu.addAction(hide_action)
        
        menu.addSeparator()
        
        # Status action
        status_action = QAction('Show Status', self)
        status_action.triggered.connect(self.show_status)
        menu.addAction(status_action)
        
        menu.addSeparator()
        
        # Exit action
        exit_action = QAction('Exit Application', self)
        exit_action.triggered.connect(self.exit_application)
        menu.addAction(exit_action)
        
        self.setContextMenu(menu)
        
        # Connect double-click
        self.activated.connect(self._on_activated)
    
    def _on_activated(self, reason):
        """Handle tray icon activation."""
        if reason == QSystemTrayIcon.DoubleClick:
            self.show_window()
    
    def show_window(self):
        """Show the main window."""
        if self.parent_window:
            self.parent_window.show()
            self.parent_window.raise_()
            self.parent_window.activateWindow()
    
    def hide_window(self):
        """Hide the main window."""
        if self.parent_window:
            self.parent_window.hide()
            self.showMessage(
                'Data Collector',
                'Application minimized to system tray',
                QSystemTrayIcon.Information,
                2000
            )
    
    def show_status(self):
        """Show current status."""
        if self.parent_window and hasattr(self.parent_window, 'get_status'):
            status = self.parent_window.get_status()
            self.showMessage(
                'Data Collector Status',
                status,
                QSystemTrayIcon.Information,
                3000
            )
    
    def exit_application(self):
        """Request application exit."""
        if self.parent_window:
            self.parent_window.close_application()
    
    def update_status(self, collecting: bool):
        """
        Update icon based on collection status.
        
        Args:
            collecting: Whether data collection is active
        """
        if collecting:
            self.setIcon(self._create_icon('green'))
            self.setToolTip('Data Collector - Collecting Data')
        else:
            self.setIcon(self._create_icon('red'))
            self.setToolTip('Data Collector - Stopped')