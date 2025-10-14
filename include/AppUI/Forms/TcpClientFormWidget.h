// TcpClientFormWidget.h - Form for editing TCP client configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonObject>
#include <QJsonDocument>
#include <vector>

/**
 * @brief Widget for editing TCP client configuration
 */
class TcpClientFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit TcpClientFormWidget(QWidget* parent = nullptr);
  ~TcpClientFormWidget() = default;

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onAddClientClicked();
  void onRemoveClientClicked(int index);

private:
  void setupUI();
  void addClientGroup(const QJsonObject& client = QJsonObject());
  void rebuildForm();

  QScrollArea* m_scrollArea;
  QWidget* m_formContent;

  // Form fields for each TCP client
  struct TcpClientFields {
    QLineEdit* id;
    QLineEdit* name;
    QLineEdit* host;
    QSpinBox* port;
    QLineEdit* dataChannel;
    QCheckBox* autoConnect;
    QCheckBox* autoReconnect;
    QLineEdit* description;
  };

  std::vector<TcpClientFields> m_fields;
};