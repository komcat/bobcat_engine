// JsonEditorWidget.cpp
#include "JsonEditorWidget.h"

#include <QFile>
#include <QTextStream>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFrame>
#include <QJsonParseError>
#include <QApplication>
#include <QToolButton>
#include <limits>

JsonEditorWidget::JsonEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void JsonEditorWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Scroll area for content
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // Content widget
    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(16, 16, 16, 16);
    m_contentLayout->setSpacing(4);
    m_contentLayout->setAlignment(Qt::AlignTop);

    // Empty state label
    m_emptyLabel = new QLabel("Select a JSON file to edit", m_contentWidget);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_contentLayout->addWidget(m_emptyLabel);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);
}

void JsonEditorWidget::loadFile(const QString& filePath)
{
    clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_emptyLabel->setText("Failed to open file: " + filePath);
        m_emptyLabel->show();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        m_emptyLabel->setText("JSON Parse Error: " + error.errorString() +
                             "\nat offset " + QString::number(error.offset));
        m_emptyLabel->show();
        return;
    }

    m_currentFile = filePath;
    m_originalDoc = doc;
    m_emptyLabel->hide();

    // Block signals during load for performance
    m_isLoading = true;

    // Disable updates during build
    m_contentWidget->setUpdatesEnabled(false);

    buildEditor(doc);

    m_contentWidget->setUpdatesEnabled(true);
    m_isLoading = false;
}

bool JsonEditorWidget::saveFile(const QString& filePath)
{
    QJsonDocument doc = collectToJson();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    // Update original state
    m_originalDoc = doc;
    m_changedPaths.clear();

    // Reset all field highlights
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        it.value().originalValue = getValueFromEditor(it.key());
        updateFieldHighlight(it.key());
    }

    return true;
}

bool JsonEditorWidget::hasUnsavedChanges() const
{
    return !m_changedPaths.isEmpty();
}

void JsonEditorWidget::clear()
{
    // Remove all widgets except empty label
    QLayoutItem* item;
    while ((item = m_contentLayout->takeAt(0)) != nullptr) {
        if (item->widget() && item->widget() != m_emptyLabel) {
            delete item->widget();
        }
        delete item;
    }

    m_fields.clear();
    m_changedPaths.clear();
    m_arrayInfo.clear();
    m_objectKeys.clear();
    m_originalDoc = QJsonDocument();
    m_currentFile.clear();

    // Re-add empty label
    m_contentLayout->addWidget(m_emptyLabel);
    m_emptyLabel->setText("Select a JSON file to edit");
    m_emptyLabel->show();
}

void JsonEditorWidget::buildEditor(const QJsonDocument& doc)
{
    if (doc.isObject()) {
        buildObjectEditor(m_contentLayout, doc.object(), "");
    } else if (doc.isArray()) {
        buildArrayEditor(m_contentLayout, doc.array(), "");
    }

    // Add stretch at end
    m_contentLayout->addStretch();
}

void JsonEditorWidget::buildObjectEditor(QVBoxLayout* layout, const QJsonObject& obj, const QString& path)
{
    QStringList keys = obj.keys();
    m_objectKeys[path] = keys;

    for (const QString& key : keys) {
        QJsonValue value = obj.value(key);
        QString fieldPath = path.isEmpty() ? key : path + "." + key;

        if (value.isObject()) {
            QWidget* group = createCollapsibleGroup(key, value.toObject(), fieldPath, false);
            layout->addWidget(group);
        } else if (value.isArray()) {
            QWidget* group = createCollapsibleArrayGroup(key, value.toArray(), fieldPath);
            layout->addWidget(group);
        } else {
            // Simple value - create form row
            QWidget* row = new QWidget();
            QHBoxLayout* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);

            QLabel* label = new QLabel(key + ":");
            label->setFixedWidth(180);

            QWidget* editor = createValueEditor(value, fieldPath);

            rowLayout->addWidget(label);
            rowLayout->addWidget(editor, 1);

            layout->addWidget(row);
        }
    }
}

void JsonEditorWidget::buildArrayEditor(QVBoxLayout* layout, const QJsonArray& arr, const QString& path)
{
    ArrayInfo info;
    info.count = arr.size();

    if (arr.isEmpty()) {
        info.itemType = "unknown";
    } else if (arr.first().isObject()) {
        info.itemType = "object";
    } else if (arr.first().isArray()) {
        info.itemType = "array";
    } else {
        info.itemType = "value";
    }

    m_arrayInfo[path] = info;

    for (int i = 0; i < arr.size(); ++i) {
        QJsonValue value = arr.at(i);
        QString itemPath = path + "[" + QString::number(i) + "]";

        if (value.isObject()) {
            QString title = "Item " + QString::number(i);
            QJsonObject itemObj = value.toObject();
            if (itemObj.contains("name")) {
                title = itemObj.value("name").toString();
            } else if (itemObj.contains("id")) {
                title = "ID: " + itemObj.value("id").toString();
            }

            QWidget* group = createCollapsibleGroup(title, itemObj, itemPath, true);
            layout->addWidget(group);
        } else if (value.isArray()) {
            QWidget* group = createCollapsibleArrayGroup("Item " + QString::number(i), value.toArray(), itemPath);
            layout->addWidget(group);
        } else {
            QWidget* editor = createValueEditor(value, itemPath);
            layout->addWidget(editor);
        }
    }
}

QWidget* JsonEditorWidget::createValueEditor(const QJsonValue& value, const QString& path)
{
    FieldInfo info;
    info.originalValue = value;

    QWidget* editor = nullptr;

    if (value.isBool()) {
        info.type = "bool";
        QCheckBox* checkbox = new QCheckBox();
        checkbox->setChecked(value.toBool());
        connect(checkbox, &QCheckBox::stateChanged, this, &JsonEditorWidget::onFieldChanged);
        editor = checkbox;
    } else if (value.isDouble()) {
        info.type = "number";
        double num = value.toDouble();

        if (num == static_cast<int>(num) && num >= std::numeric_limits<int>::min() && num <= std::numeric_limits<int>::max()) {
            QSpinBox* spinbox = new QSpinBox();
            spinbox->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            spinbox->setValue(static_cast<int>(num));
            connect(spinbox, QOverload<int>::of(&QSpinBox::valueChanged), this, &JsonEditorWidget::onFieldChanged);
            editor = spinbox;
        } else {
            QDoubleSpinBox* spinbox = new QDoubleSpinBox();
            spinbox->setRange(-1e15, 1e15);
            spinbox->setDecimals(10);
            spinbox->setValue(num);
            connect(spinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &JsonEditorWidget::onFieldChanged);
            editor = spinbox;
        }
    } else if (value.isString()) {
        info.type = "string";
        QLineEdit* lineEdit = new QLineEdit();
        lineEdit->setText(value.toString());
        connect(lineEdit, &QLineEdit::textChanged, this, &JsonEditorWidget::onFieldChanged);
        editor = lineEdit;
    } else if (value.isNull()) {
        info.type = "null";
        QLineEdit* lineEdit = new QLineEdit();
        lineEdit->setText("null");
        lineEdit->setEnabled(false);
        editor = lineEdit;
    }

    if (editor) {
        info.widget = editor;
        m_fields[path] = info;
    }

    return editor;
}

QWidget* JsonEditorWidget::createCollapsibleGroup(const QString& title, const QJsonObject& obj, const QString& path, bool isArrayItem)
{
    QWidget* container = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 2, 0, 2);
    containerLayout->setSpacing(0);

    // Header button
    QToolButton* header = new QToolButton();
    header->setText(QString::fromUtf8("\u25BC ") + title);  // Down arrow
    header->setToolButtonStyle(Qt::ToolButtonTextOnly);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setCheckable(true);
    header->setChecked(true);

    // Style array items differently
    if (isArrayItem) {
        header->setProperty("arrayItem", true);
    }

    // Content area
    QWidget* content = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 8, 0, 8);
    contentLayout->setSpacing(4);

    // Build content
    buildObjectEditor(contentLayout, obj, path);

    // Connect toggle
    connect(header, &QToolButton::toggled, [header, content](bool checked) {
        content->setVisible(checked);
        header->setText((checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) +
                       header->text().mid(2));
    });

    containerLayout->addWidget(header);
    containerLayout->addWidget(content);

    return container;
}

QWidget* JsonEditorWidget::createCollapsibleArrayGroup(const QString& title, const QJsonArray& arr, const QString& path)
{
    QWidget* container = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 2, 0, 2);
    containerLayout->setSpacing(0);

    // Header button with item count
    QString headerText = title + " [" + QString::number(arr.size()) + " items]";
    QToolButton* header = new QToolButton();
    header->setText(QString::fromUtf8("\u25BC ") + headerText);
    header->setToolButtonStyle(Qt::ToolButtonTextOnly);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setCheckable(true);
    header->setChecked(true);
    header->setProperty("arrayItem", true);

    // Content area
    QWidget* content = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 8, 0, 8);
    contentLayout->setSpacing(4);

    // Build content
    buildArrayEditor(contentLayout, arr, path);

    // Connect toggle
    connect(header, &QToolButton::toggled, [header, content, headerText](bool checked) {
        content->setVisible(checked);
        header->setText((checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + headerText);
    });

    containerLayout->addWidget(header);
    containerLayout->addWidget(content);

    return container;
}

QWidget* JsonEditorWidget::createObjectGroup(const QString& key, const QJsonObject& obj, const QString& path)
{
    return createCollapsibleGroup(key, obj, path, false);
}

QWidget* JsonEditorWidget::createArrayGroup(const QString& key, const QJsonArray& arr, const QString& path)
{
    return createCollapsibleArrayGroup(key, arr, path);
}

void JsonEditorWidget::onFieldChanged()
{
    // Skip during loading
    if (m_isLoading) return;

    // Find which field changed
    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (!sender) return;

    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        if (it.value().widget == sender) {
            markFieldAsChanged(it.key());
            break;
        }
    }

    emit contentChanged();
}

void JsonEditorWidget::markFieldAsChanged(const QString& path)
{
    QJsonValue currentValue = getValueFromEditor(path);
    QJsonValue originalValue = m_fields[path].originalValue;

    bool changed = (currentValue != originalValue);

    if (changed) {
        m_changedPaths.insert(path);
    } else {
        m_changedPaths.remove(path);
    }

    updateFieldHighlight(path);
}

void JsonEditorWidget::updateFieldHighlight(const QString& path)
{
    if (!m_fields.contains(path)) return;

    QWidget* widget = m_fields[path].widget;
    bool isChanged = m_changedPaths.contains(path);

    // Use property for stylesheet to pick up
    widget->setProperty("changed", isChanged);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QJsonValue JsonEditorWidget::getValueFromEditor(const QString& path) const
{
    if (!m_fields.contains(path)) {
        return QJsonValue();
    }

    const FieldInfo& info = m_fields[path];
    QWidget* widget = info.widget;

    if (info.type == "bool") {
        QCheckBox* checkbox = qobject_cast<QCheckBox*>(widget);
        return checkbox ? QJsonValue(checkbox->isChecked()) : QJsonValue();
    } else if (info.type == "number") {
        if (QSpinBox* spinbox = qobject_cast<QSpinBox*>(widget)) {
            return QJsonValue(spinbox->value());
        } else if (QDoubleSpinBox* dspinbox = qobject_cast<QDoubleSpinBox*>(widget)) {
            return QJsonValue(dspinbox->value());
        }
    } else if (info.type == "string") {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget);
        return lineEdit ? QJsonValue(lineEdit->text()) : QJsonValue();
    } else if (info.type == "null") {
        return QJsonValue(QJsonValue::Null);
    }

    return QJsonValue();
}

QJsonDocument JsonEditorWidget::collectToJson() const
{
    if (m_originalDoc.isObject()) {
        QJsonObject root = collectObject("");
        return QJsonDocument(root);
    } else if (m_originalDoc.isArray()) {
        ArrayInfo info = m_arrayInfo.value("");
        QJsonArray root = collectArray("", info.count);
        return QJsonDocument(root);
    }
    return QJsonDocument();
}

QJsonObject JsonEditorWidget::collectObject(const QString& basePath) const
{
    QJsonObject obj;

    QStringList keys = m_objectKeys.value(basePath);
    for (const QString& key : keys) {
        QString fieldPath = basePath.isEmpty() ? key : basePath + "." + key;

        if (m_fields.contains(fieldPath)) {
            obj[key] = getValueFromEditor(fieldPath);
        }
        else if (m_objectKeys.contains(fieldPath)) {
            obj[key] = collectObject(fieldPath);
        }
        else if (m_arrayInfo.contains(fieldPath)) {
            ArrayInfo info = m_arrayInfo[fieldPath];
            obj[key] = collectArray(fieldPath, info.count);
        }
    }

    return obj;
}

QJsonArray JsonEditorWidget::collectArray(const QString& basePath, int count) const
{
    QJsonArray arr;

    for (int i = 0; i < count; ++i) {
        QString itemPath = basePath + "[" + QString::number(i) + "]";

        if (m_fields.contains(itemPath)) {
            arr.append(getValueFromEditor(itemPath));
        }
        else if (m_objectKeys.contains(itemPath)) {
            arr.append(collectObject(itemPath));
        }
        else if (m_arrayInfo.contains(itemPath)) {
            ArrayInfo info = m_arrayInfo[itemPath];
            arr.append(collectArray(itemPath, info.count));
        }
    }

    return arr;
}
