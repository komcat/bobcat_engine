"""Data plotting widget for real-time visualization."""

import pyqtgraph as pg
from PyQt5.QtWidgets import QWidget, QVBoxLayout


class DataPlotter(QWidget):
    """Widget for plotting real-time data and histograms."""
    
    def __init__(self, max_points: int = 500):
        """
        Initialize the plotter.
        
        Args:
            max_points: Maximum number of data points to display
        """
        super().__init__()
        self.max_data_points = max_points
        self.data_x = []
        self._init_ui()
    
    def _init_ui(self):
        """Initialize the user interface."""
        layout = QVBoxLayout(self)
        
        # Create plot widget
        self.plot_widget = pg.GraphicsLayoutWidget()
        
        # Create real-time plot
        self.plot_item = self.plot_widget.addPlot(
            title="Real-time Channel 1 Current Measurement"
        )
        self.curve = self.plot_item.plot(pen={'color': 'yellow', 'width': 3})
        
        # Create histogram plot
        self.histogram_item = self.plot_widget.addPlot(
            title="Histogram of Elapsed Times"
        )
        self.histogram = pg.BarGraphItem(x=[], height=[], width=0.1, brush='r')
        self.histogram_item.addItem(self.histogram)
        
        layout.addWidget(self.plot_widget)
    
    def update_plot(self, data: float):
        """
        Update the real-time plot with new data.
        
        Args:
            data: New data point
        """
        self.data_x.append(data)
        self.curve.setData(self.data_x[-self.max_data_points:])
    
    def update_histogram(self, hist_data: list):
        """
        Update the histogram plot.
        
        Args:
            hist_data: List containing [histogram values, bin edges]
        """
        hist, bin_edges = hist_data
        width = bin_edges[1] - bin_edges[0] if len(bin_edges) > 1 else 0.1
        self.histogram.setOpts(x=bin_edges[:-1], height=hist, width=width)
    
    def clear(self):
        """Clear all plot data."""
        self.data_x.clear()
        self.curve.setData([])
        self.histogram.setOpts(x=[], height=[], width=0.1)