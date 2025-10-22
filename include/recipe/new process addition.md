# Adding New Process Sequence Actions - Quick Guide

## Overview
To add a new process type to the Recipe system, modify 4 files (or 6 if creating a new device type).

---

## Steps

### 1. **RecipeManager.cpp** - Define Process Type
```cpp
// A) Add to list
getAvailableProcessTypes() {
  return { "Pick", "Place", "Your New Process" };
}

// B) Add default parameters
getDefaultParameters(processType) {
  else if (processType == "Your New Process") {
    params["param1"] = "default_value";
    params["param2"] = "default_value";
  }
}
```

### 2. **ProcessSequenceFactory.cpp** - Register Builder
```cpp
ProcessSequenceFactory::ProcessSequenceFactory() {
  RegisterProcessBuilder("Your New Process", 
    [](SequenceBuilder* seq, const ProcessInstance* step) {
      const auto& params = step->parameters;
      
      seq->AddAction("device_type", "MethodName", {
        {"param1", params.at("param1")},
        {"param2", params.at("param2")}
      });
  });
}
```

### 3. **ProcessConfigWidget.h** - Declare Widget
```cpp
class YourProcessConfigWidget : public ProcessConfigWidget {
  Q_OBJECT
public:
  explicit YourProcessConfigWidget(ApplicationContext* ctx, QWidget* parent = nullptr);
protected:
  void createParameterFields(const ProcessInstance* instance) override;
};
```

### 4. **ProcessConfigWidget.cpp** - Implement Widget
```cpp
void YourProcessConfigWidget::createParameterFields(const ProcessInstance* instance) {
  const auto& params = instance->parameters;
  
  QLineEdit* edit = new QLineEdit(QString::fromStdString(params.at("param1")));
  connect(edit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Param 1:", edit);
  m_parameterEdits["param1"] = edit;
}
```

### 5. **RecipePage.cpp** - Register Widget
```cpp
void RecipePage::updateConfigurationForm() {
  // ... existing code ...
  else if (instance->processType == "Your New Process") {
    m_currentConfigWidget = new YourProcessConfigWidget(m_context);
  }
}
```

---

## If Creating New Device Type (Optional)

### 6a. **YourCommandHandler.h**
```cpp
class YourCommandHandler : public ICommandHandler {
public:
  YourCommandHandler(YourManager* manager);
  bool ExecuteCommand(...) override;
  std::string GetManagerType() const override { return "yourtype"; }
};
```

### 6b. **SequenceEngine.cpp** - Register Handler
```cpp
// In constructor:
m_dispatcher.RegisterHandler("yourtype", 
  std::make_unique<YourCommandHandler>(m_context->yourManager));

// In BuildDeviceRegistry():
if (m_context->yourManager) {
  auto devices = m_context->yourManager->GetAllDeviceNames();
  for (const auto& name : devices) {
    m_deviceToManagerMap[name] = m_context->yourManager;
    m_deviceToTypeMap[name] = "yourtype";
  }
}
```

---

## Checklist
- [ ] RecipeManager: Add to process types + default params
- [ ] ProcessSequenceFactory: Register builder
- [ ] ProcessConfigWidget.h: Declare widget class
- [ ] ProcessConfigWidget.cpp: Implement widget
- [ ] RecipePage.cpp: Register widget creation
- [ ] (Optional) Create command handler for new device type
- [ ] (Optional) Register handler in SequenceEngine
- [ ] Build and test

---

## Example: Read DataStore Process
See `DataStoreCommandHandler.h/cpp` for a complete working example.