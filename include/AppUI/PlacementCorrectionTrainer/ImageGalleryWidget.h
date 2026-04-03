// ImageGalleryWidget.h - Gallery view for saved reference images
#pragma once

#include <QWidget>
#include <QLabel>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

/**
 * @brief Displays the 4 reference images (Ref P1, Ref P2, Tgt P1, Tgt P2)
 * in a 2x2 grid gallery view with labels and position info.
 */
class ImageGalleryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImageGalleryWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const PlacementCorrectionSet& set);

private:
    void setupUI();
    void loadImage(QLabel* imageLabel, QLabel* infoLabel,
                   const std::string& imageFile, const QString& pointName,
                   double px, double py, double pz, double exposure);
    std::string getImageDirectory() const;

    ApplicationContext* m_context;
    PlacementCorrectionSet m_set;

    QLabel* m_refP1Image = nullptr;
    QLabel* m_refP1Info = nullptr;
    QLabel* m_refP2Image = nullptr;
    QLabel* m_refP2Info = nullptr;
    QLabel* m_tgtP1Image = nullptr;
    QLabel* m_tgtP1Info = nullptr;
    QLabel* m_tgtP2Image = nullptr;
    QLabel* m_tgtP2Info = nullptr;
};
