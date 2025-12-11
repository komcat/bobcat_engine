// ContextImageViewer.h
// Non-modal popup window for displaying images from SequenceContext
#pragma once

#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QString>
#include <QTimer>

#ifdef HAS_OPENCV
#include <opencv2/core.hpp>
#endif

/**
 * @brief Non-modal popup window for displaying context images
 *
 * Used by "Show Context Image" command to display captured images
 * from the sequence context. The window is non-blocking and can
 * be closed by the user or automatically.
 */
class ContextImageViewer : public QDialog
{
    Q_OBJECT

public:
    explicit ContextImageViewer(const QString& title, QWidget* parent = nullptr);
    ~ContextImageViewer() = default;

#ifdef HAS_OPENCV
    // Set image from cv::Mat
    void setImage(const cv::Mat& image);
#endif

    // Set image from file path
    void setImageFromFile(const QString& filePath);

    // Set auto-close timer (0 = no auto-close)
    void setAutoCloseMs(int ms);

    // Static helper to show image from context key
    // Returns immediately (non-blocking)
    static void ShowContextImage(const QString& contextKey,
                                 const QString& title,
                                 int autoCloseMs = 0,
                                 QWidget* parent = nullptr);

#ifdef HAS_OPENCV
    // Static helper to show cv::Mat image directly
    static void ShowImage(const cv::Mat& image,
                         const QString& title,
                         int autoCloseMs = 0,
                         QWidget* parent = nullptr);
#endif

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onCloseClicked();
    void onAutoClose();

private:
    void setupUI();
    void updateImageDisplay();

    QLabel* m_imageLabel;
    QScrollArea* m_scrollArea;
    QPushButton* m_closeButton;
    QTimer* m_autoCloseTimer;

    QPixmap m_pixmap;
    QString m_title;
};
