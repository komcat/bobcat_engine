// VisionHistoryWidget.h
// Vision result image viewer with prev/next history navigation.
// Used as the body of the "Vision" tab in RunProgramPage's right panel.
//
// The widget owns:
//   - the QGroupBox/QLabel display for the current image,
//   - a small ◀ Prev / "n / total" / Next ▶ navigation row,
//   - a FIFO ring buffer of up to kMaxHistory recent image paths.
//
// Behaviour mirrors the previous in-line code in RunProgramPage.cpp;
// the worker still writes to logs/vision_result_temp.png before signalling
// onVisionResultReady, which calls AddImage() here. Other code paths that
// previously called updateVisionResultImage() / clearVisionResult() now
// call RefreshFromTempFile() / ClearAndResetDisplay().
#pragma once

#include <QGroupBox>
#include <QString>
#include <vector>

class QLabel;
class QPushButton;

class VisionHistoryWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit VisionHistoryWidget(QWidget* parent = nullptr);

    // Append a new image path. Drops the oldest entry once the buffer is
    // full (FIFO). Resets the view to "latest".
    void AddImage(const QString& imagePath);

    // Drop all history. Display goes back to its "No vision result"
    // placeholder.
    void Clear();

    // Pull logs/vision_result_temp.png and render it without touching the
    // history list. Used right after the worker writes the temp file.
    void RefreshFromTempFile();

    // Drop the temp file from disk + reset the label + clear history.
    void ClearAndResetDisplay();

private slots:
    void onPrevClicked();
    void onNextClicked();

private:
    void updateNavigation();
    void loadImage(int index);
    void renderPixmapFromPath(const QString& path, bool isHistoryView);

    QLabel*      m_imageLabel  = nullptr;
    QPushButton* m_prevButton  = nullptr;
    QPushButton* m_nextButton  = nullptr;
    QLabel*      m_indexLabel  = nullptr;

    std::vector<QString> m_history;
    int m_viewIndex = -1;   // -1 = showing latest in history

    static constexpr int kMaxHistory = 10;
};
