// TopBar.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class TECManager;

struct ApplicationContext;
class SafetyWatchManager;
class ScanActivityTracker;

/**
 * @brief Visual indicator for SafetyWatchManager status
 *
 * States:
 * - Running: Green pulsing animation (cycling brightness)
 * - Stopped: Gray (no animation)
 * - Alert: Red blinking animation
 */
class SafetyIndicatorWidget : public QWidget
{
  Q_OBJECT

public:
  enum class State {
    Stopped,    // Gray, no animation
    Running,    // Green pulsing
    Alert       // Red blinking
  };

  explicit SafetyIndicatorWidget(QWidget* parent = nullptr);
  ~SafetyIndicatorWidget() override;

  void setState(State state);
  State state() const { return m_state; }

  void setToolTipText(const QString& text);

protected:
  void paintEvent(QPaintEvent* event) override;

private slots:
  void onAnimationTick();

private:
  State m_state;
  QTimer* m_animationTimer;
  int m_animationPhase;  // 0-100 for smooth transitions
  bool m_blinkOn;        // For alert blinking
};

/**
 * @brief Fixed top bar widget that appears on all pages
 *
 * Features:
 * - Displays product name permanently
 * - Contains JOG button for jog panel access
 * - Height: 50px
 * - Always visible across all pages
 */
class TopBar : public QWidget
{
  Q_OBJECT

public:
  explicit TopBar(ApplicationContext* context, QWidget* parent = nullptr);
  ~TopBar();

  /**
   * @brief Update the displayed product information
   * @param productName Product name to display
   * @param workingPath Working folder path (optional)
   */
  void setProductInfo(const QString& productName, const QString& workingPath = "");

  /**
   * @brief Get the JOG button widget
   * @return Pointer to JOG button
   */
  QPushButton* getJogButton() const { return m_jogButton; }

  /**
   * @brief Get the safety indicator widget
   * @return Pointer to safety indicator
   */
  SafetyIndicatorWidget* getSafetyIndicator() const { return m_safetyIndicator; }

  /**
   * @brief Connect the safety indicator to SafetyWatchManager
   * @param manager Pointer to SafetyWatchManager
   */
  void connectSafetyManager(SafetyWatchManager* manager);

  /**
   * @brief Wire the scan-busy indicator to the ScanActivityTracker.
   * The "⚡ SCANNING" badge is shown while any scan is active (tab-initiated
   * or recipe-initiated via MotionCommandHandler's GridScan/XCAN commands).
   * Null tracker is a no-op.
   */
  void connectScanActivityTracker(ScanActivityTracker* tracker);

  /**
   * @brief Start TEC temperature polling in the top bar
   * Reads temperature from the first connected TEC device and displays it.
   */
  void startTECTemperaturePolling();

signals:
  void jogButtonClicked();
  void auxWindowButtonClicked();
  void rtlmButtonClicked();
  void rtlm2ButtonClicked();
  void swirWindowButtonClicked();
  void gdiButtonClicked();

public slots:
  /**
   * @brief Re-apply all local stylesheets using current Theme::Color::* values.
   * Call this whenever the active theme changes so that TopBar colors update
   * without recreating any widgets.
   */
  void applyTheme();

private:
  void setupUI();

  ApplicationContext* m_context;
  QLabel* m_productNameLabel;
  QLabel* m_workingPathLabel;
  QPushButton* m_jogButton;
  QPushButton* m_auxWindowButton;
  QPushButton* m_rtlmButton;
  QPushButton* m_rtlm2Button;
  QPushButton* m_gdiButton;
  QPushButton* m_swirButton;
  SafetyIndicatorWidget* m_safetyIndicator;
  QLabel* m_scanBusyLabel;       // ⚡ SCANNING badge, hidden when idle
  QLabel* m_tecTemperatureLabel;
  QTimer* m_tecPollTimer;
};
