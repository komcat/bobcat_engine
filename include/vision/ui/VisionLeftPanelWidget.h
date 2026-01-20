// VisionLeftPanelWidget.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QVBoxLayout>

class VisionLeftPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionLeftPanelWidget(QWidget* parent = nullptr);

    void updatePluginList(const QStringList& plugins);
    void updatePluginsByCategory(const QStringList& prPlugins,
                                  const QStringList& measurePlugins,
                                  const QStringList& filterPlugins,
                                  const QStringList& calibrationPlugins = QStringList());
    void displayResult(const QString& resultText);
    QString getSelectedPlugin() const;
    void clearPluginLists();

signals:
    void addToNodeClicked();
    void pluginSelected(const QString& pluginName);

private:
    void setupUI();
    void populateMockupData();

    QPushButton* m_addToNodeBtn;
    QTabWidget* m_pluginTabs;
    QListWidget* m_prList;
    QListWidget* m_measureList;
    QListWidget* m_filterList;
    QListWidget* m_calibrationList;  // Calibration plugins tab
    QListWidget* m_lightingList;  // Lighting commands tab
    QTextEdit* m_resultsDisplay;
};
