keithley_data_collector/
├── main.py                 # Entry point
├── config.py              # Configuration settings
├── core/
│   ├── __init__.py
│   ├── data_collector.py  # DataCollector class
│   └── server.py          # TCP server functionality
├── hardware/
│   ├── __init__.py
│   └── multimeter.py      # Keithley multimeter interface
├── ui/
│   ├── __init__.py
│   ├── main_window.py     # MainWindow class
│   ├── data_plotter.py    # DataPlotter widget
│   └── system_tray.py     # SystemTrayIcon class
└── utils/
    ├── __init__.py
    └── signals.py         # Custom signals if needed