"""Alignment-specific plotter with log scale support."""

from ui.dual_channel_plotter import DualChannelPlotter
import pyqtgraph as pg
import numpy as np
from collections import deque


class AlignmentPlotter(DualChannelPlotter):
    """Enhanced plotter for alignment applications."""
    
    def __init__(self, max_points: int = 500):
        super().__init__(max_points)
        self._setup_log_scale_option()
        
        # Add update counter to reduce plotting frequency
        self._update_counter = 0
    
    def _setup_log_scale_option(self):
        """Add option for log scale display (useful for wide dynamic range)."""
        # Add log scale option to plots
        for plot in [self.combined_plot, self.ch1_plot_individual, self.ch2_plot_individual]:
            plot.setLogMode(x=False, y=False)  # Can be toggled
            plot.showGrid(x=True, y=True, alpha=0.3)
            # Add performance optimizations
            plot.setDownsampling(mode='peak')
            plot.setClipToView(True)
    
    def update_ch1_plot(self, data: float):
        """Update channel 1 with handling for wide range."""
        # Limit data list size to prevent unbounded growth
        if not hasattr(self, 'ch1_data'):
            self.ch1_data = deque(maxlen=self.max_data_points)
        
        # Store absolute value for log scale compatibility
        self.ch1_data.append(abs(data) if data != 0 else 1e-12)
        
        # Reduce update frequency
        self._update_counter += 1
        if self._update_counter % 2 == 0:  # Update every 2nd call
            super().update_ch1_plot(data)
    
    def update_ch2_plot(self, data: float):
        """Update channel 2 with handling for wide range."""
        # Limit data list size
        if not hasattr(self, 'ch2_data'):
            self.ch2_data = deque(maxlen=self.max_data_points)
        
        self.ch2_data.append(abs(data) if data != 0 else 1e-12)
        
        # Reduce update frequency (offset from ch1)
        if self._update_counter % 2 == 1:
            super().update_ch2_plot(data)