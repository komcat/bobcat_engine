// VisionProfileTesterWindow.h
#pragma once

#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QTextEdit>
#include <QScrollArea>
#include <QPushButton>
#include <QProgressBar>
#include <QSplitter>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <opencv2/core.hpp>
#include "nlohmann/json.hpp"
#include "VisionResult.h"
#include "VisionROI.h"

#include <map>
#include <string>

class VisionProfileTesterWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit VisionProfileTesterWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSelectProfile();
    void onSelectFolder();
    void onRunAll();
    void onRunSelected();
    void onImageSelected(int row);
    void onExportCsv();
    void onClearResults();
    void onReloadProfile();
    void onSaveProfile();
    void onSaveAsProfile();
    void onToggleSettings(bool checked);
    void onApplyParams();

private:
    void setupUI();
    void registerPlugins();
    void loadProfileJson(const QString& path);
    void loadFolder(const QString& path);
    void processImage(int index);
    void displayResult(int index);
    void resetAllListItems();
    void updateStatusBar(const QString& message);

    // Settings persistence
    void saveSettings();
    void loadSettings();

    // Settings panel
    QWidget* createSettingsPanel();
    QWidget* createROITab();
    QWidget* createSearchBoxTab();
    QWidget* createPluginTab();
    void buildPluginParamsUI();
    void populateSettingsFromProfile();
    nlohmann::json collectPluginParamsFromUI();
    void updateROISizeLabel();
    void updateSearchBoxSizeLabel();

    // UI widgets
    QLineEdit* m_profilePath = nullptr;
    QLineEdit* m_folderPath = nullptr;
    QListWidget* m_imageList = nullptr;
    QLabel* m_imageDisplay = nullptr;
    QTextEdit* m_resultsText = nullptr;
    QLabel* m_statusLabel = nullptr;
    QScrollArea* m_imageScrollArea = nullptr;
    QLabel* m_calibrationLabel = nullptr;
    QPushButton* m_runAllBtn = nullptr;
    QPushButton* m_runSelectedBtn = nullptr;
    QPushButton* m_exportCsvBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Profile file ops
    QPushButton* m_reloadBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_saveAsBtn = nullptr;

    // Settings panel
    QPushButton* m_settingsToggleBtn = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QWidget* m_settingsPanel = nullptr;

    // ROI tab
    QCheckBox* m_roiEnabledCheck = nullptr;
    QSpinBox* m_roiStartX = nullptr;
    QSpinBox* m_roiStartY = nullptr;
    QSpinBox* m_roiEndX = nullptr;
    QSpinBox* m_roiEndY = nullptr;
    QLabel* m_roiSizeLabel = nullptr;

    // SearchBox tab
    QCheckBox* m_searchEnabledCheck = nullptr;
    QSpinBox* m_searchStartX = nullptr;
    QSpinBox* m_searchStartY = nullptr;
    QSpinBox* m_searchEndX = nullptr;
    QSpinBox* m_searchEndY = nullptr;
    QLabel* m_searchSizeLabel = nullptr;

    // Calibration
    QDoubleSpinBox* m_calXSpin = nullptr;
    QDoubleSpinBox* m_calYSpin = nullptr;

    // Plugin params
    std::map<std::string, QWidget*> m_paramWidgets;
    QVBoxLayout* m_pluginParamsLayout = nullptr;

    // Data
    struct ImageEntry {
        QString filePath;
        VisionResult result;
        cv::Mat outputImage;
        bool processed = false;
    };
    std::vector<ImageEntry> m_images;

    // Profile data
    QString m_pluginName;
    nlohmann::json m_pluginParams;
    nlohmann::json m_profileJson;  // full original JSON for round-trip save
    QString m_loadedProfilePath;   // path of currently loaded profile
    bool m_profileLoaded = false;
    double m_pixelToMmX = 0.00248;
    double m_pixelToMmY = 0.00248;
};
