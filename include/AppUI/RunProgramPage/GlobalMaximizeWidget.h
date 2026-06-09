// GlobalMaximizeWidget.h
#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;

/**
 * @brief UI section for the dual-hexapod "global maximize" routine.
 *
 * A self-contained QGroupBox shown below the existing "Hexapod Scanning" panel
 * on the Run Program page's Scanning tab. Lets the operator pick the shared
 * signal channel, beam direction, and the routine's step/grid/settle options,
 * then Start / Stop the routine. Emits intent only — no business logic.
 */
class GlobalMaximizeWidget : public QWidget {
  Q_OBJECT

public:
  explicit GlobalMaximizeWidget(QWidget* parent = nullptr);
  ~GlobalMaximizeWidget() override = default;

  // Channel list (shared with the scan widget).
  void    setAvailableChannels(const QStringList& channels);
  QString currentChannel() const;
  void    setCurrentChannel(const QString& channel);

  // Live value display.
  void setDisplayText(const QString& text);

  // Toggle UI between idle and running.
  void setRunningState(bool running);

  // Resolved parameters for the controller.
  QString zStepDevice() const;   ///< "hex-left" / "hex-right" (steps -Z)
  QString gridDevice() const;    ///< "hex-right" / "hex-left" (XY grid)
  double  zStepMm() const;
  double  gridStepMm() const;
  int     gridSize() const;
  int     settleMs() const;

signals:
  void startRequested();
  void stopRequested();

private:
  void setupUI();
  bool isLeftToRight() const;

  QComboBox*   m_channelCombo   = nullptr;
  QComboBox*   m_directionCombo = nullptr;
  QComboBox*   m_zStepCombo     = nullptr;
  QComboBox*   m_gridStepCombo  = nullptr;
  QComboBox*   m_gridSizeCombo  = nullptr;
  QComboBox*   m_settleCombo    = nullptr;
  QLabel*      m_valueLabel     = nullptr;
  QPushButton* m_startButton    = nullptr;
  QPushButton* m_stopButton     = nullptr;
};
