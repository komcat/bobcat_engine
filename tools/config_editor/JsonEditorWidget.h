// JsonEditorWidget.h
#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QGroupBox>

/**
 * @brief Right panel widget for editing JSON content dynamically
 *
 * Features:
 * - Dynamically generates input fields based on JSON structure
 * - Supports nested objects and arrays
 * - Highlights changed fields (compared to loaded state)
 * - Type-aware editing (string, number, boolean, null)
 */
class JsonEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit JsonEditorWidget(QWidget* parent = nullptr);
    ~JsonEditorWidget() = default;

    void loadFile(const QString& filePath);
    bool saveFile(const QString& filePath);
    bool hasUnsavedChanges() const;
    void clear();

signals:
    void contentChanged();

private slots:
    void onFieldChanged();

private:
    void setupUI();
    void buildEditor(const QJsonDocument& doc);
    void buildObjectEditor(QVBoxLayout* layout, const QJsonObject& obj, const QString& path = "");
    void buildArrayEditor(QVBoxLayout* layout, const QJsonArray& arr, const QString& path);
    QWidget* createValueEditor(const QJsonValue& value, const QString& path);
    QWidget* createObjectGroup(const QString& key, const QJsonObject& obj, const QString& path);
    QWidget* createArrayGroup(const QString& key, const QJsonArray& arr, const QString& path);
    QWidget* createCollapsibleGroup(const QString& title, const QJsonObject& obj, const QString& path, bool isArrayItem);
    QWidget* createCollapsibleArrayGroup(const QString& title, const QJsonArray& arr, const QString& path);

    QJsonValue getValueFromEditor(const QString& path) const;
    QJsonDocument collectToJson() const;
    QJsonObject collectObject(const QString& basePath) const;
    QJsonArray collectArray(const QString& basePath, int count) const;

    void updateFieldHighlight(const QString& path);
    void markFieldAsChanged(const QString& path);

    // UI Components
    QScrollArea* m_scrollArea;
    QWidget* m_contentWidget;
    QVBoxLayout* m_contentLayout;
    QLabel* m_emptyLabel;

    // Editor state
    struct FieldInfo {
        QWidget* widget;
        QJsonValue originalValue;
        QString type;  // "string", "number", "bool", "null", "object", "array"
    };
    QMap<QString, FieldInfo> m_fields;  // path -> field info
    QSet<QString> m_changedPaths;
    QJsonDocument m_originalDoc;
    QString m_currentFile;

    // Track structure for reconstruction
    struct ArrayInfo {
        int count;
        QString itemType;
    };
    QMap<QString, ArrayInfo> m_arrayInfo;  // path -> array info
    QMap<QString, QStringList> m_objectKeys;  // path -> ordered list of keys

    // Loading state
    bool m_isLoading = false;
};
