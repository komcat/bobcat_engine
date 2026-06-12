// M4AASerialDataWidget.h - M4AA branch-specific serial-number data viewer.
//
// Embedded as a tab ("M4AA") in the F1 Run Program page. Replicates
// serverPython/view_serial_data.py:
//   - A routine worker (1 Hz QTimer) watches the active product's
//     data/{product}/ folder for the most recently modified *.json result
//     file and re-reads it whenever its mtime changes, so the view stays live.
//   - Left panel : entries list (timestamp, step, location), newest first.
//   - Right panel: B0/B1 poynt X/Y grid for MOE-A1 vs MOE-A2 (green if
//     |value| < 0.5, red otherwise) plus a scatter plot with an r=0.5
//     acceptance circle. Hover a cell for the hex-right correction hint.
//
// Self-contained: it tracks the active product itself (via the poll tick),
// so the host page does not need to wire any product-change signal.
#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <vector>

class QTreeWidget;
class QLabel;
class QTimer;
class QDialog;
class QPushButton;
struct ApplicationContext;

namespace m4aa {

// One B0 or B1 beam section of an entry's poynt measurement.
struct PoyntSection {
  bool   hasX = false;
  bool   hasY = false;
  double x    = 0.0;
  double y    = 0.0;
  // Additional beam-detect-server fields written alongside the poynt angles.
  bool   hasLoc   = false;
  double locX     = 0.0;   // beam centroid X (µm)
  double locY     = 0.0;   // beam centroid Y (µm)
  bool   hasZ0    = false;
  double z0       = 0.0;   // beam-waist Z (µm)
  bool   hasWaist = false;
  double waistX   = 0.0;   // 1/e² waist X (µm)
  double waistY   = 0.0;
  bool   hasM2    = false;
  double m2X      = 0.0;   // beam-quality M² in X
  double m2Y      = 0.0;
};

// TX-RX delta (b0 - b1) for the entry's gantry location.
struct TxRx {
  bool   has = false;
  double dx  = 0.0;        // µm
  double dy  = 0.0;        // µm
  double dz  = 0.0;        // µm
  double mag = 0.0;        // µm
};

// One serial-number result entry parsed from the JSON file.
struct SerialEntry {
  QString      timestamp;
  QString      key;        // step name, e.g. "step1_beam_fit"
  QString      location;   // "MOE-A1" / "MOE-A2"
  PoyntSection b0;
  PoyntSection b1;
  TxRx         txRx;
};

// Scatter plot: A1/A2 B0/B1 poynt points on a +/-5 deg grid with an r=0.5
// acceptance circle. Plain QWidget (no signals/slots, so no MOC needed).
class PoyntScatterWidget : public QWidget {
public:
  struct Point {
    QString label;
    double  x = 0.0;
    double  y = 0.0;
    QColor  color;
  };

  explicit PoyntScatterWidget(QWidget* parent = nullptr);
  void setPoints(const std::vector<Point>& pts);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  std::vector<Point> m_points;
};

}  // namespace m4aa

class M4AASerialDataWidget : public QWidget {
  Q_OBJECT

public:
  explicit M4AASerialDataWidget(ApplicationContext* context,
                                QWidget* parent = nullptr);

private slots:
  void onTick();              // routine worker: poll the watch folder
  void onSelectionChanged();  // left-list selection -> refresh right panel
  void onSaveFinalMeasurement();  // export latest A1+A2 pair to <serial>_final.json
  void onOpenDataFolder();        // open the watch folder in Explorer

private:
  void    setupUI();
  QString watchDir() const;   // applicationDir/data/{currentProduct}
  void    reload(const QString& path, bool jumpToNewest);
  void    populateList();
  void    updateRight(int selIdx);
  void    clearRight();
  int     findLatestByLocation(int uptoIdx, const QString& loc) const;
  void    ensureDetailsDialog();         // lazy-build the non-modal popup
  void    openDetailsDialog();
  void    refreshDetailsDialog(int selIdx, int a1Idx, int a2Idx);
  void    clearDetailsDialog();

  ApplicationContext* m_context = nullptr;

  QLabel*      m_header       = nullptr;
  QLabel*      m_status       = nullptr;
  QTreeWidget* m_tree         = nullptr;
  QLabel*      m_detailHeader = nullptr;
  m4aa::PoyntScatterWidget* m_scatter = nullptr;
  QTimer*      m_timer        = nullptr;

  // 8 poynt value labels, indexed [section 0=b0/1=b1][side 0=a1/1=a2][axis 0=x/1=y].
  QLabel* m_valueLabels[2][2][2] = {};

  // Non-modal details popup (built lazily on first open).
  QPushButton* m_detailsButton = nullptr;
  QDialog*     m_detailsDialog = nullptr;

  // Exports the latest MOE-A1 + MOE-A2 entries (the final pass/fail state) to
  // data/final_measurement/<serial>_final.json.
  QPushButton* m_saveFinalButton = nullptr;

  // Opens the monitored data folder (applicationDir/data/<product>) in Explorer.
  QPushButton* m_openFolderButton = nullptr;

  // Labels inside the details dialog. One section per location (MOE-A1, MOE-A2);
  // each shows poynt X/Y for both beams + tx_rx delta + loc/waist/M².
  struct BeamDetailLabels {
    QLabel* poyntX = nullptr;
    QLabel* poyntY = nullptr;
    QLabel* locX   = nullptr;
    QLabel* locY   = nullptr;
    QLabel* z0     = nullptr;
    QLabel* waistX = nullptr;
    QLabel* waistY = nullptr;
    QLabel* m2X    = nullptr;
    QLabel* m2Y    = nullptr;
  };
  struct LocationDetailLabels {
    QLabel* header = nullptr;   // "MOE-A1  -  <timestamp>"
    QLabel* dx     = nullptr;
    QLabel* dy     = nullptr;
    BeamDetailLabels b0;
    BeamDetailLabels b1;
  };
  struct DetailLabels {
    LocationDetailLabels a1;
    LocationDetailLabels a2;
  } m_details;

  std::vector<m4aa::SerialEntry> m_entries;
  QString m_currentPath;
  QString m_lastWatchDir;     // detects product switches between ticks
  long long m_currentMtime = -1;

  // Top-level metadata of the loaded file (for the final-measurement export).
  QString m_serialNumber;
  QString m_productName;
  QString m_createdTimestamp;
};
