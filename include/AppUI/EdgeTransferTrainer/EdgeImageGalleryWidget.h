// EdgeImageGalleryWidget.h - 2x2 gallery of captured edge-point images
#pragma once

#include <QWidget>
#include <QLabel>
#include "EdgeTransferData.h"

struct ApplicationContext;

/**
 * @brief Displays the 4 captured edge-point images (Ref P1, Ref P2,
 * Tgt P1 / P3, Tgt P2 / P4) in a 2x2 grid with position info and a
 * center-25% crop + red crosshair, mirroring PlacementCorrectionTrainer.
 */
class EdgeImageGalleryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeImageGalleryWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const EdgeTransferSet& set);

private:
    void setupUI();
    void loadImage(QLabel* imageLabel, QLabel* infoLabel,
                   const std::string& imageFile, const QString& pointName,
                   double px, double py, double pz);
    std::string getImageDirectory() const;

    ApplicationContext* m_context;
    EdgeTransferSet m_set;

    QLabel* m_refP1Image = nullptr;
    QLabel* m_refP1Info  = nullptr;
    QLabel* m_refP2Image = nullptr;
    QLabel* m_refP2Info  = nullptr;
    QLabel* m_tgtP1Image = nullptr;
    QLabel* m_tgtP1Info  = nullptr;
    QLabel* m_tgtP2Image = nullptr;
    QLabel* m_tgtP2Info  = nullptr;
};
