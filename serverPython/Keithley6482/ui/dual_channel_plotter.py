"""Dual channel data plotting widget for real-time visualization."""

import pyqtgraph as pg
from PyQt5.QtWidgets import QWidget, QVBoxLayout, QTabWidget


class DualChannelPlotter(QWidget):
    """Widget for plotting real-time data from two channels."""
    
    def __init__(self, max_points: int = 500):
        """
        Initialize the dual channel plotter.
        
        Args:
            max_points: Maximum number of data points to display
        """
        super().__init__()
        self.max_data_points = max_points
        self.ch1_data = []
        self.ch2_data = []
        self._init_ui()
    
    def _init_ui(self):
        """Initialize the user interface."""
        layout = QVBoxLayout(self)
        
        # Create tab widget for channel views
        self.tab_widget = QTabWidget()
        
        # Combined view tab
        self.combined_widget = pg.GraphicsLayoutWidget()
        self._setup_combined_view()
        self.tab_widget.addTab(self.combined_widget, "Combined View")
        
        # Channel 1 tab
        self.ch1_widget = pg.GraphicsLayoutWidget()
        self._setup_channel_view(1, self.ch1_widget)
        self.tab_widget.addTab(self.ch1_widget, "Channel 1")
        
        # Channel 2 tab
        self.ch2_widget = pg.GraphicsLayoutWidget()
        self._setup_channel_view(2, self.ch2_widget)
        self.tab_widget.addTab(self.ch2_widget, "Channel 2")
        
        layout.addWidget(self.tab_widget)
    
    def _setup_combined_view(self):
        """Setup the combined view showing both channels."""
        # Create plot for both channels
        self.combined_plot = self.combined_widget.addPlot(
            title="Real-time Current Measurement - Both Channels",
            colspan=2
        )
        self.combined_plot.addLegend()
        
        # Channel 1 curve (yellow)
        self.ch1_curve_combined = self.combined_plot.plot(
            pen={'color': 'yellow', 'width': 3},
            name='Channel 1'
        )
        
        # Channel 2 curve (cyan)
        self.ch2_curve_combined = self.combined_plot.plot(
            pen={'color': 'cyan', 'width': 3},
            name='Channel 2'
        )
        
        # Add next row for histograms
        self.combined_widget.nextRow()
        
        # Channel 1 histogram
        self.ch1_hist_combined = self.combined_widget.addPlot(
            title="Ch1 Timing Histogram"
        )
        self.ch1_histogram_combined = pg.BarGraphItem(
            x=[], height=[], width=0.1, brush='y'
        )
        self.ch1_hist_combined.addItem(self.ch1_histogram_combined)
        
        # Channel 2 histogram
        self.ch2_hist_combined = self.combined_widget.addPlot(
            title="Ch2 Timing Histogram"
        )
        self.ch2_histogram_combined = pg.BarGraphItem(
            x=[], height=[], width=0.1, brush='c'
        )
        self.ch2_hist_combined.addItem(self.ch2_histogram_combined)
    
    def _setup_channel_view(self, channel: int, widget: pg.GraphicsLayoutWidget):
        """
        Setup individual channel view.
        
        Args:
            channel: Channel number (1 or 2)
            widget: Graphics widget to setup
        """
        # Create plot
        plot = widget.addPlot(
            title=f"Channel {channel} Current Measurement"
        )
        
        # Create curve with appropriate color
        color = 'yellow' if channel == 1 else 'cyan'
        curve = plot.plot(pen={'color': color, 'width': 3})
        
        # Store references
        if channel == 1:
            self.ch1_plot_individual = plot
            self.ch1_curve_individual = curve
        else:
            self.ch2_plot_individual = plot
            self.ch2_curve_individual = curve
        
        # Add histogram below
        widget.nextRow()
        hist_plot = widget.addPlot(
            title=f"Channel {channel} Timing Histogram"
        )
        
        hist_color = 'y' if channel == 1 else 'c'
        histogram = pg.BarGraphItem(
            x=[], height=[], width=0.1, brush=hist_color
        )
        hist_plot.addItem(histogram)
        
        # Store histogram references
        if channel == 1:
            self.ch1_histogram_individual = histogram
        else:
            self.ch2_histogram_individual = histogram
    
    def update_ch1_plot(self, data: float):
        """Update channel 1 plots with new data."""
        self.ch1_data.append(data)
        plot_data = self.ch1_data[-self.max_data_points:]
        
        # Update combined view
        self.ch1_curve_combined.setData(plot_data)
        
        # Update individual view
        self.ch1_curve_individual.setData(plot_data)
    
    def update_ch2_plot(self, data: float):
        """Update channel 2 plots with new data."""
        self.ch2_data.append(data)
        plot_data = self.ch2_data[-self.max_data_points:]
        
        # Update combined view
        self.ch2_curve_combined.setData(plot_data)
        
        # Update individual view
        self.ch2_curve_individual.setData(plot_data)
    
    def update_ch1_histogram(self, hist_data: list):
        """Update channel 1 histograms."""
        hist, bin_edges = hist_data
        width = bin_edges[1] - bin_edges[0] if len(bin_edges) > 1 else 0.1
        
        # Update both views
        self.ch1_histogram_combined.setOpts(x=bin_edges[:-1], height=hist, width=width)
        self.ch1_histogram_individual.setOpts(x=bin_edges[:-1], height=hist, width=width)
    
    def update_ch2_histogram(self, hist_data: list):
        """Update channel 2 histograms."""
        hist, bin_edges = hist_data
        width = bin_edges[1] - bin_edges[0] if len(bin_edges) > 1 else 0.1
        
        # Update both views
        self.ch2_histogram_combined.setOpts(x=bin_edges[:-1], height=hist, width=width)
        self.ch2_histogram_individual.setOpts(x=bin_edges[:-1], height=hist, width=width)
    
    def clear(self):
        """Clear all plot data."""
        self.ch1_data.clear()
        self.ch2_data.clear()
        
        # Clear all curves
        self.ch1_curve_combined.setData([])
        self.ch2_curve_combined.setData([])
        self.ch1_curve_individual.setData([])
        self.ch2_curve_individual.setData([])