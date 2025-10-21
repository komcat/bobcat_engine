// PneumaticSlideSelectorDialog.h
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <string>
#include <vector>

struct ApplicationContext;

/**
 * @brief Dialog for selecting pneumatic slides with live status display
 */
class PneumaticSlideSelectorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit PneumaticSlideSelectorDialog(ApplicationContext* context,
    const std::string& currentSlide,
    QWidget* parent = nullptr);

  // Get selected slide name
  std::string getSelectedSlide() const;

  // Static convenience method
  static std::string SelectSlide(ApplicationContext* context,
    const std::string& currentSlide,
    QWidget* parent = nullptr);

private:
  void setupUI();
  void loadSlides();
  void refreshSlideStatus();
  void onSlideDoubleClicked(QListWidgetItem* item);
  void onSearchTextChanged(const QString& text);

  ApplicationContext* m_context;
  std::string m_selectedSlide;

  QListWidget* m_slideList;
  QLineEdit* m_searchEdit;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
  QPushButton* m_refreshButton;

  std::vector<std::string> m_allSlides;
};