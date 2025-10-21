// PneumaticSlideSelectorDialog.cpp
#include "PneumaticSlideSelectorDialog.h"
#include "ApplicationContext.h"
#include "PneumaticManager.h"
#include "Pneumatic_Types.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <iostream>

PneumaticSlideSelectorDialog::PneumaticSlideSelectorDialog(ApplicationContext* context,
  const std::string& currentSlide,
  QWidget* parent)
  : QDialog(parent)
  , m_context(context)
  , m_selectedSlide(currentSlide)
{
  setupUI();
  loadSlides();

  // Select current slide if exists
  for (int i = 0; i < m_slideList->count(); ++i) {
    QListWidgetItem* item = m_slideList->item(i);
    QString itemText = item->text();

    // Extract slide name (before the status indicator)
    QString slideName = itemText.split(" [").first().trimmed();

    if (slideName.toStdString() == currentSlide) {
      m_slideList->setCurrentRow(i);
      break;
    }
  }
}

void PneumaticSlideSelectorDialog::setupUI()
{
  setWindowTitle("Select Pneumatic Slide");
  setMinimumSize(450, 550);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(15, 15, 15, 15);
  mainLayout->setSpacing(10);

  // Info label
  QLabel* infoLabel = new QLabel("Select a pneumatic slide:");
  infoLabel->setStyleSheet("font-weight: bold; font-size: 13px; padding: 5px;");
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search slides...");
  m_searchEdit->setStyleSheet("padding: 8px; font-size: 12px; border: 1px solid #ccc; border-radius: 4px;");
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &PneumaticSlideSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Slide list
  m_slideList = new QListWidget();
  m_slideList->setStyleSheet(
    "QListWidget { "
    "  background-color: white; "
    "  border: 1px solid #c0c0c0; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  padding: 5px; "
    "} "
    "QListWidget::item { "
    "  padding: 10px; "
    "  border-bottom: 1px solid #e0e0e0; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "}"
  );
  connect(m_slideList, &QListWidget::itemDoubleClicked,
    this, &PneumaticSlideSelectorDialog::onSlideDoubleClicked);
  mainLayout->addWidget(m_slideList);

  // Status legend
  QLabel* legendLabel = new QLabel(
    "Status: 🟢 Extended  |  🔴 Retracted  |  🟡 Moving  |  ⚪ Unknown"
  );
  legendLabel->setStyleSheet("font-size: 11px; color: #666; padding: 5px;");
  mainLayout->addWidget(legendLabel);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  m_refreshButton = new QPushButton("🔄 Refresh Status");
  m_refreshButton->setMinimumWidth(120);
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #95a5a6; "
    "  color: white; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  padding: 8px; "
    "} "
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_refreshButton, &QPushButton::clicked, this, &PneumaticSlideSelectorDialog::refreshSlideStatus);
  buttonLayout->addWidget(m_refreshButton);

  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #95a5a6; "
    "  color: white; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  padding: 8px; "
    "} "
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-weight: bold; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  padding: 8px; "
    "} "
    "QPushButton:hover { background-color: #45a049; }"
  );

  connect(m_okButton, &QPushButton::clicked, [this]() {
    if (m_slideList->currentItem()) {
      QString itemText = m_slideList->currentItem()->text();

      // ✅ Remove emoji and extract slide name properly
      // Remove emoji prefix
      if (itemText.startsWith("🟢") || itemText.startsWith("🔴") ||
        itemText.startsWith("🟡") || itemText.startsWith("⚪") ||
        itemText.startsWith("❌") || itemText.startsWith("⏱")) {
        itemText = itemText.mid(2).trimmed();  // Skip emoji + space
      }

      // Extract slide name (before the status bracket)
      QString slideName = itemText.split(" [").first().trimmed();

      m_selectedSlide = slideName.toStdString();
      accept();
    }
  });
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);
}

void PneumaticSlideSelectorDialog::loadSlides()
{
  m_slideList->clear();
  m_allSlides.clear();

  if (!m_context || !m_context->pneumaticManager) {
    m_slideList->addItem("⚠ Pneumatic manager not available");
    return;
  }

  // Get all slide names
  m_allSlides = m_context->pneumaticManager->GetAllSlideNames();

  if (m_allSlides.empty()) {
    m_slideList->addItem("⚠ No pneumatic slides configured");
    return;
  }

  std::cout << "[PneumaticSlideSelectorDialog] Loaded " << m_allSlides.size()
    << " slides" << std::endl;

  // Add slides with status
  for (const auto& slideName : m_allSlides) {
    // Get current position
    PneumaticPosition pos = m_context->pneumaticManager->GetPosition(slideName);

    // Create display text with emoji status
    QString displayText = QString::fromStdString(slideName);
    QString statusEmoji;
    QString statusColor;

    switch (pos) {
    case PneumaticPosition::EXTENDED:
      statusEmoji = "🟢";
      statusColor = "#27ae60";
      displayText += " [Extended]";
      break;
    case PneumaticPosition::RETRACTED:
      statusEmoji = "🔴";
      statusColor = "#e74c3c";
      displayText += " [Retracted]";
      break;
    case PneumaticPosition::EXTENDING:
      statusEmoji = "🟡";
      statusColor = "#f39c12";
      displayText += " [Extending...]";
      break;
    case PneumaticPosition::RETRACTING:
      statusEmoji = "🟡";
      statusColor = "#f39c12";
      displayText += " [Retracting...]";
      break;
    case PneumaticPosition::HARDWARE_ERROR:
      statusEmoji = "❌";
      statusColor = "#c0392b";
      displayText += " [ERROR]";
      break;
    case PneumaticPosition::TIMEOUT:
      statusEmoji = "⏱";
      statusColor = "#e67e22";
      displayText += " [Timeout]";
      break;
    default:
      statusEmoji = "⚪";
      statusColor = "#95a5a6";
      displayText += " [Unknown]";
      break;
    }

    QListWidgetItem* item = new QListWidgetItem(statusEmoji + " " + displayText);
    item->setForeground(QColor(statusColor));
    m_slideList->addItem(item);
  }
}

void PneumaticSlideSelectorDialog::refreshSlideStatus()
{
  std::cout << "[PneumaticSlideSelectorDialog] Refreshing slide status..." << std::endl;

  // Remember current selection
  int currentRow = m_slideList->currentRow();

  // Reload slides with updated status
  loadSlides();

  // Restore selection
  if (currentRow >= 0 && currentRow < m_slideList->count()) {
    m_slideList->setCurrentRow(currentRow);
  }
}

void PneumaticSlideSelectorDialog::onSearchTextChanged(const QString& text)
{
  // Filter list based on search text
  std::string search = text.toLower().toStdString();

  for (int i = 0; i < m_slideList->count(); ++i) {
    QListWidgetItem* item = m_slideList->item(i);
    QString itemText = item->text().toLower();

    bool matches = search.empty() || itemText.contains(QString::fromStdString(search));
    item->setHidden(!matches);
  }
}

void PneumaticSlideSelectorDialog::onSlideDoubleClicked(QListWidgetItem* item)
{
  QString itemText = item->text();

  // ✅ Remove emoji and extract slide name properly
  // Format is: "🟢 SlideName [Status]"
  // Remove emoji prefix
  if (itemText.startsWith("🟢") || itemText.startsWith("🔴") ||
    itemText.startsWith("🟡") || itemText.startsWith("⚪") ||
    itemText.startsWith("❌") || itemText.startsWith("⏱")) {
    itemText = itemText.mid(2).trimmed();  // Skip emoji + space
  }

  // Extract slide name (before the status bracket)
  QString slideName = itemText.split(" [").first().trimmed();

  m_selectedSlide = slideName.toStdString();
  accept();
}

std::string PneumaticSlideSelectorDialog::getSelectedSlide() const
{
  return m_selectedSlide;
}

std::string PneumaticSlideSelectorDialog::SelectSlide(ApplicationContext* context,
  const std::string& currentSlide,
  QWidget* parent)
{
  PneumaticSlideSelectorDialog dialog(context, currentSlide, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedSlide();
  }

  return currentSlide; // Return original if cancelled
}