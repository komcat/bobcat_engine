# Implementing Conditionals in Recipe System

## Overview

Your SequenceContext system already has the infrastructure for conditionals. Here's how to implement if-then-else decision making in recipes.

---

## 🔧 Step 1: Update SequenceBuilder (Code Level)

### Add New BeginIf Overload

**File**: `include/sequence/SequenceBuilder.h`

Add this new method signature:
```cpp
// Context-based conditional (NEW)
void BeginIf(const std::string& contextKey,
             ConditionalBlock::ComparisonType comparison,
             float threshold = 0.0f);

// Old method (keep for backward compatibility)
void BeginIf(const std::string& conditionDevice,
             const std::string& conditionMethod);
```

**File**: `include/sequence/SequenceEngine.cpp` (SequenceBuilder implementation)

Add this implementation after the existing BeginIf:
```cpp
void SequenceBuilder::BeginIf(const std::string& contextKey,
                               ConditionalBlock::ComparisonType comparison,
                               float threshold)
{
  auto cond = std::make_shared<ConditionalBlock>(contextKey, comparison, threshold);

  // Add to current active list
  GetActiveStepList()->push_back(cond);

  // Push to stack (THEN branch)
  BlockContext ctx;
  ctx.type = BlockType::CONDITIONAL_THEN;
  ctx.block = cond;
  m_blockStack.push_back(ctx);
}
```

---

## 📝 Step 2: Programmatic Usage (Testing)

Once the new BeginIf is added, you can test it programmatically:

```cpp
// Example: Check if voltage is within acceptable range
SequenceBuilder* seq = m_engine->CreateSequence("VoltageCheck");

// Step 1: Measure voltage and capture it
seq->AddAction("PSU1", "ReadVoltage", {{"captureAs", "V"}});

// Step 2: Check if V > 3.0
seq->BeginIf("V", ConditionalBlock::ComparisonType::GREATER_THAN, 3.0f);
    // THEN branch - voltage is good
    seq->AddAction("system", "UserPrompt", {
        {"title", "Success"},
        {"message", "Voltage is above 3.0V"},
        {"blocking", "false"}
    });
seq->Else();
    // ELSE branch - voltage is too low
    seq->AddAction("system", "UserPrompt", {
        {"title", "Warning"},
        {"message", "Voltage is too low!"},
        {"blocking", "true"}
    });
seq->EndIf();

auto context = std::make_unique<SequenceContext>();
m_engine->ExecuteSequence(seq, context.get());
```

---

## 🎯 Step 3: Add to Recipe System

To use conditionals in the Recipe UI, you need to:

### 3.1 Define Conditional Process Types

**File**: `include/recipe/RecipeManager.cpp`

Add new process types to `getAvailableProcessTypes()`:
```cpp
types.push_back("Conditional If");
types.push_back("Conditional Else");
types.push_back("Conditional End");
```

### 3.2 Add Default Parameters

**File**: `include/recipe/RecipeManager.cpp` in `getDefaultParameters()`

```cpp
else if (processType == "Conditional If") {
  params["context_key"] = "MeasuredValue";
  params["comparison"] = ">";  // >, <, ==, !=, >=, <=, IS_TRUE, IS_FALSE, EXISTS, NOT_EXISTS
  params["threshold"] = "0";
}
else if (processType == "Conditional Else") {
  // No parameters needed
}
else if (processType == "Conditional End") {
  // No parameters needed
}
```

### 3.3 Register Process Builders

**File**: `include/recipe/ProcessSequenceFactory.cpp`

Add these builders in the constructor:

```cpp
// ============================================================================
// REGISTER CONDITIONAL IF PROCESS BUILDER
// ============================================================================
RegisterProcessBuilder("Conditional If", [](SequenceBuilder* sequence, const ProcessInstance* step) {
  const auto& params = step->parameters;

  std::string contextKey = params.at("context_key");
  std::string comparisonStr = params.at("comparison");
  float threshold = std::stof(params.at("threshold"));

  // Convert comparison string to enum
  ConditionalBlock::ComparisonType comparison;
  if (comparisonStr == ">") comparison = ConditionalBlock::ComparisonType::GREATER_THAN;
  else if (comparisonStr == "<") comparison = ConditionalBlock::ComparisonType::LESS_THAN;
  else if (comparisonStr == "==") comparison = ConditionalBlock::ComparisonType::EQUAL;
  else if (comparisonStr == "!=") comparison = ConditionalBlock::ComparisonType::NOT_EQUAL;
  else if (comparisonStr == ">=") comparison = ConditionalBlock::ComparisonType::GREATER_OR_EQUAL;
  else if (comparisonStr == "<=") comparison = ConditionalBlock::ComparisonType::LESS_OR_EQUAL;
  else if (comparisonStr == "IS_TRUE") comparison = ConditionalBlock::ComparisonType::IS_TRUE;
  else if (comparisonStr == "IS_FALSE") comparison = ConditionalBlock::ComparisonType::IS_FALSE;
  else if (comparisonStr == "EXISTS") comparison = ConditionalBlock::ComparisonType::EXISTS;
  else if (comparisonStr == "NOT_EXISTS") comparison = ConditionalBlock::ComparisonType::NOT_EXISTS;
  else comparison = ConditionalBlock::ComparisonType::GREATER_THAN; // default

  sequence->BeginIf(contextKey, comparison, threshold);

  std::cout << "[ProcessSequenceFactory] Built Conditional If: "
            << contextKey << " " << comparisonStr << " " << threshold << std::endl;
});

// ============================================================================
// REGISTER CONDITIONAL ELSE PROCESS BUILDER
// ============================================================================
RegisterProcessBuilder("Conditional Else", [](SequenceBuilder* sequence, const ProcessInstance* step) {
  sequence->Else();
  std::cout << "[ProcessSequenceFactory] Built Conditional Else" << std::endl;
});

// ============================================================================
// REGISTER CONDITIONAL END PROCESS BUILDER
// ============================================================================
RegisterProcessBuilder("Conditional End", [](SequenceBuilder* sequence, const ProcessInstance* step) {
  sequence->EndIf();
  std::cout << "[ProcessSequenceFactory] Built Conditional End" << std::endl;
});
```

### 3.4 Create UI Widgets

**File**: `include/AppUI/RecipePage/ProcessConfigWidget.h`

Add widget declarations:
```cpp
class ConditionalIfConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT
public:
  explicit ConditionalIfConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);
protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

class ConditionalElseConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT
public:
  explicit ConditionalElseConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);
protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

class ConditionalEndConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT
public:
  explicit ConditionalEndConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);
protected:
  void createParameterFields(const ProcessInstance* instance) override;
};
```

**File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp`

Add implementations:
```cpp
// ============================================================================
// CONDITIONAL IF CONFIG WIDGET
// ============================================================================
ConditionalIfConfigWidget::ConditionalIfConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalIfConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* condLabel = new QLabel("Condition:");
  condLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(condLabel);

  // Context key field
  std::string contextKey = params.count("context_key") ? params.at("context_key") : "MeasuredValue";
  QLineEdit* keyEdit = new QLineEdit(QString::fromStdString(contextKey));
  keyEdit->setPlaceholderText("Variable name from context (e.g., MeasuredVoltage)");
  connect(keyEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Context Key:", keyEdit);
  m_parameterEdits["context_key"] = keyEdit;

  // Comparison dropdown
  std::string comparison = params.count("comparison") ? params.at("comparison") : ">";
  QLineEdit* compEdit = new QLineEdit(QString::fromStdString(comparison));
  m_parameterEdits["comparison"] = compEdit;

  QComboBox* compCombo = new QComboBox();
  compCombo->addItem(">");
  compCombo->addItem("<");
  compCombo->addItem("==");
  compCombo->addItem("!=");
  compCombo->addItem(">=");
  compCombo->addItem("<=");
  compCombo->addItem("IS_TRUE");
  compCombo->addItem("IS_FALSE");
  compCombo->addItem("EXISTS");
  compCombo->addItem("NOT_EXISTS");
  compCombo->setCurrentText(QString::fromStdString(comparison));

  connect(compCombo, &QComboBox::currentTextChanged, [this, compEdit](const QString& text) {
    compEdit->setText(text);
    emit parameterChanged();
  });
  m_formLayout->addRow("Comparison:", compCombo);

  // Threshold field
  std::string threshold = params.count("threshold") ? params.at("threshold") : "0";
  addParameterField("threshold", "Threshold Value", threshold);

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Evaluates the condition. If TRUE, executes steps until 'Conditional Else' or 'Conditional End'. "
    "If FALSE, executes 'Conditional Else' branch if present.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// CONDITIONAL ELSE CONFIG WIDGET
// ============================================================================
ConditionalElseConfigWidget::ConditionalElseConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalElseConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // No parameters needed - just visual indicator
  QLabel* infoLabel = new QLabel(
    "<i><b>ELSE Branch</b><br>"
    "Steps after this will execute if the condition is FALSE.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// CONDITIONAL END CONFIG WIDGET
// ============================================================================
ConditionalEndConfigWidget::ConditionalEndConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalEndConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // No parameters needed - just visual indicator
  QLabel* infoLabel = new QLabel(
    "<i><b>END Conditional</b><br>"
    "Marks the end of the conditional block.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}
```

### 3.5 Register Widgets

**File**: `include/AppUI/RecipePage/RecipePage.cpp` in `updateConfigurationForm()`

Add widget registration:
```cpp
else if (processType == "Conditional If") {
  configWidget = new ConditionalIfConfigWidget(m_context);
}
else if (processType == "Conditional Else") {
  configWidget = new ConditionalElseConfigWidget(m_context);
}
else if (processType == "Conditional End") {
  configWidget = new ConditionalEndConfigWidget(m_context);
}
```

---

## 📊 Example Recipe with Conditionals

Once implemented, users can create recipes like this:

```json
{
  "recipe_name": "Voltage Check with Decision",
  "steps": [
    {
      "process_type": "PSU Read Voltage",
      "parameters": {
        "psu_device": "PSU1",
        "read_count": "3",
        "delay_ms": "100",
        "captureAs": "V"
      }
    },
    {
      "process_type": "Conditional If",
      "parameters": {
        "context_key": "V",
        "comparison": ">=",
        "threshold": "3.0"
      }
    },
    {
      "process_type": "Set Output",
      "parameters": {
        "device": "IOTop",
        "pin": "GreenLED",
        "state": "true"
      }
    },
    {
      "process_type": "Conditional Else"
    },
    {
      "process_type": "Set Output",
      "parameters": {
        "device": "IOTop",
        "pin": "RedLED",
        "state": "true"
      }
    },
    {
      "process_type": "Conditional End"
    }
  ]
}
```

---

## 🎯 Summary

To implement conditionals:

1. ✅ **Update SequenceBuilder** - Add new context-based BeginIf overload
2. ✅ **Add Process Types** - "Conditional If", "Conditional Else", "Conditional End"
3. ✅ **Register Builders** - Convert recipe params to SequenceBuilder calls
4. ✅ **Create Widgets** - UI for configuring conditions
5. ✅ **Test!** - Create test recipes with conditionals

The infrastructure is already there - you just need to connect it to the Recipe UI!

---

## 🚀 Quick Start

Want me to implement these changes for you? I can:
1. Add the new BeginIf overload to SequenceBuilder
2. Add conditional process types to RecipeManager
3. Create the process builders in ProcessSequenceFactory
4. Add the UI widgets in ProcessConfigWidget

Just let me know!
