// MotionPage.cpp
#include "MotionPage.h"
#include "AppUI/DarkTheme.h"
#include "NewPositionDialog.h"
// #include "WaypointEditorDialog.h"  // ❌ REMOVED: Waypoints not used
#include "OffsetMoveDialog.h"  // ✅ Add at top
#include "EditPositionDialog.h"
#include "ScanControlPanel.h"
#include "FileSelectorDialog.h"  // ✅ Custom file selector
#include "CreateSystemWidget.h"
#include "TestSystemWidget.h"
#include "SequenceBuilder.h"     // ✅ NEW: For sequence execution
#include "SequenceContext.h"     // ✅ NEW: For sequence execution
#include "graph/MotionGraph.h"
#include "graph/MotionGraphManager.h"
#include "products/product_position_manager.h"
#include <iostream>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QDebug>
#include <QShowEvent>  // ✅ NEW: For showEvent override
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>

MotionPage::MotionPage(UnifiedMotionManager* motionManager,
	ProductPositionManager* productManager,
	TransformationMatrixLoader* matrixLoader,
	DataStore* dataStore,
	CoordinateSystemManager* coordinateSystemManager,
	SequenceEngine* sequenceEngine,
	InterlockManager* interlockManager,
	QWidget* parent)
	: QWidget(parent)
	, m_motionManager(motionManager)
	, m_productManager(productManager)
	, m_matrixLoader(matrixLoader)
	, m_dataStore(dataStore)
	, m_coordinateSystemManager(coordinateSystemManager)
	, m_sequenceEngine(sequenceEngine)
	, m_interlockManager(interlockManager)
	, m_tabWidget(nullptr)
	, m_controlPanel(nullptr)
	, m_graphicsView(nullptr)
	, m_scanPanel(nullptr)
	, m_graphWidget(nullptr)
	, m_scanController(nullptr)
	, m_livePositionTimer(nullptr)
	, m_motionUpdateTimer(nullptr)
	// Label offsets removed — isometric view uses plain text labels
{
	if (!m_productManager) {
		std::cout << "[ERROR] MotionPage: ProductPositionManager is NULL!" << std::endl;
	}
	else {
		std::cout << "[INFO] MotionPage: ProductPositionManager at: "
			<< (void*)m_productManager << std::endl;

		try {
			bool hasProduct = m_productManager->HasLoadedProduct();
			std::cout << "[INFO] MotionPage: HasLoadedProduct() = " << hasProduct << std::endl;
		}
		catch (...) {
			std::cout << "[ERROR] MotionPage: ProductPositionManager not properly initialized!" << std::endl;
		}
	}

	setupUI();
	loadDevices();
	// Product is already loaded at application level - just update UI
	if (m_productManager && m_productManager->HasLoadedProduct()) {
		// Export positions to motion manager
		m_productManager->ExportToMotionManager(*m_motionManager);

		// Update product label to show name and filename
		std::string productName = m_productManager->GetCurrentProductName();
		std::string productPath = m_productManager->GetCurrentProductPath();
		QString filename = QFileInfo(QString::fromStdString(productPath)).fileName();

		m_currentProductLabel->setText(
			QString("Product: %1 | File: %2")
			.arg(QString::fromStdString(productName))
			.arg(filename)
		);

		// Refresh position display if device selected
		if (!m_currentDevice.isEmpty()) {
			refreshPositionDisplay();
		}
	}

	// Setup live position polling timer
	m_livePositionTimer = new QTimer(this);
	m_livePositionTimer->setInterval(100);
	connect(m_livePositionTimer, &QTimer::timeout, this, &MotionPage::onUpdateLivePosition);
	m_livePositionTimer->start();

	// ✅ Setup motion manager update timer
	m_motionUpdateTimer = new QTimer(this);
	m_motionUpdateTimer->setInterval(100);
	connect(m_motionUpdateTimer, &QTimer::timeout, this, [this]() {
		m_motionManager->Update();  // Update waypoint state machines
	});
	m_motionUpdateTimer->start();

	// Label offsets removed - isometric view uses plain text labels
}

MotionPage::~MotionPage()
{
	// Stop timers before destruction to prevent callbacks on invalid object
	if (m_livePositionTimer) {
		m_livePositionTimer->stop();
	}
	if (m_motionUpdateTimer) {
		m_motionUpdateTimer->stop();
	}

	// Disconnect all signals to prevent queued invocations after destruction
	disconnect();

	qDebug() << "[MotionPage] Destroyed";
}

void MotionPage::setupUI()
{
	// Set dark theme for entire page
	setStyleSheet(
		"MotionPage { "
		"  background-color: #2d2d30; "
		"  color: #d4d4d4; "
		"} "
		"QWidget { "
		"  color: #d4d4d4; "
		"} "
		"QLabel { "
		"  color: #d4d4d4; "
		"}"
	);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Top bar with back button
	QWidget* topBar = new QWidget();
	topBar->setStyleSheet("QWidget { background-color: #2d2d30; border-bottom: 2px solid #3f3f46; }");
	topBar->setFixedHeight(60);

	QHBoxLayout* topLayout = new QHBoxLayout(topBar);
	topLayout->setContentsMargins(15, 10, 15, 10);

	// Back button
	m_backButton = new QPushButton("← Back to Main Menu");
	m_backButton->setFixedHeight(40);
	m_backButton->setStyleSheet(
		"QPushButton {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a8fd6, stop:1 #3d5a8c);"
		"  color: white;"
		"  font-size: 14px;"
		"  font-weight: bold;"
		"  padding: 8px 20px;"
		"  border-top: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-right: 2px solid rgba(0, 0, 0, 0.5);"
		"}"
		"QPushButton:hover {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a9fe6, stop:1 #5a8fd6);"
		"  border-top: 2px solid rgba(255, 255, 255, 0.6);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.6);"
		"}"
		"QPushButton:pressed {"
		"  background-color: #2d4a7c;"
		"  border-top: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-left: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
		"  border-right: 2px solid rgba(255, 255, 255, 0.3);"
		"  padding-top: 10px;"
		"  padding-left: 22px;"
		"}"
	);
	topLayout->addWidget(m_backButton);

	// Title
	QLabel* titleLabel = new QLabel("Motion Control");
	titleLabel->setStyleSheet(
		"QLabel {"
		"  color: #ffffff;"
		"  font-size: 20px;"
		"  font-weight: bold;"
		"}"
	);
	topLayout->addWidget(titleLabel, 1, Qt::AlignCenter);

	// Product controls on the right
	QHBoxLayout* productLayout = new QHBoxLayout();

	m_currentProductLabel = new QLabel("No product loaded");
	m_currentProductLabel->setStyleSheet(
		"QLabel {"
		"  color: #cccccc;"
		"  font-size: 12px;"
		"  font-weight: bold;"
		"  padding: 5px;"
		"}"
	);
	productLayout->addWidget(m_currentProductLabel);

	m_loadProductBtn = new QPushButton("Import Positions");
	m_loadProductBtn->setFixedHeight(35);
	m_loadProductBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db);"
		"  color: white;"
		"  font-size: 12px;"
		"  padding: 5px 15px;"
		"  border-top: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-right: 2px solid rgba(0, 0, 0, 0.5);"
		"}"
		"QPushButton:hover {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2);"
		"  border-top: 2px solid rgba(255, 255, 255, 0.6);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.6);"
		"}"
		"QPushButton:pressed {"
		"  background-color: #2874a6;"
		"  border-top: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-left: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
		"  border-right: 2px solid rgba(255, 255, 255, 0.3);"
		"  padding-top: 7px;"
		"  padding-left: 17px;"
		"}"
	);
	productLayout->addWidget(m_loadProductBtn);

	m_saveProductBtn = new QPushButton("Save Positions");
	m_saveProductBtn->setFixedHeight(35);
	m_saveProductBtn->setEnabled(true);  // ✅ Always enabled - will prompt for save location if needed
	m_saveProductBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2ecc71, stop:1 #27ae60);"
		"  color: white;"
		"  font-size: 12px;"
		"  padding: 5px 15px;"
		"  border-top: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.5);"
		"  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-right: 2px solid rgba(0, 0, 0, 0.5);"
		"}"
		"QPushButton:hover {"
		"  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3edc81, stop:1 #2ecc71);"
		"  border-top: 2px solid rgba(255, 255, 255, 0.6);"
		"  border-left: 2px solid rgba(255, 255, 255, 0.6);"
		"}"
		"QPushButton:pressed {"
		"  background-color: #1e8449;"
		"  border-top: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-left: 2px solid rgba(0, 0, 0, 0.5);"
		"  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
		"  border-right: 2px solid rgba(255, 255, 255, 0.3);"
		"  padding-top: 7px;"
		"  padding-left: 17px;"
		"}"
		"QPushButton:disabled {"
		"  background-color: #7f8c8d;"
		"  color: #bdc3c7;"
		"  border-top: 1px solid rgba(255, 255, 255, 0.2);"
		"  border-left: 1px solid rgba(255, 255, 255, 0.2);"
		"  border-bottom: 1px solid rgba(0, 0, 0, 0.3);"
		"  border-right: 1px solid rgba(0, 0, 0, 0.3);"
		"}"
	);
	productLayout->addWidget(m_saveProductBtn);

	topLayout->addLayout(productLayout);

	mainLayout->addWidget(topBar);

	// ============================================================================
	// MAIN CONTENT LAYOUT - Device Stack | Tab Widget
	// ============================================================================
	QHBoxLayout* contentLayout = new QHBoxLayout();
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);

	// Far Left - Device Selection Buttons (controls all tabs)
	m_deviceButtonStack = new DeviceButtonStack();
	m_deviceButtonStack->setMotionManager(m_motionManager);  // ✅ NEW: Pass motion manager for connection status
	contentLayout->addWidget(m_deviceButtonStack);

	// ============================================================================
	// ✅ TAB WIDGET - Motion Control | Coordinate Systems | Graph
	// ============================================================================
	m_tabWidget = new QTabWidget();
	m_tabWidget->setStyleSheet(
		"QTabWidget::pane {"
		"  border: none;"
		"  background-color: #2d2d30;"
		"}"
		"QTabBar {"
		"  background-color: #2d2d30;"
		"  border: none;"
		"}"
		"QTabBar::tab {"
		"  background-color: #1e1e1e;"
		"  color: #888888;"
		"  padding: 10px 20px;"
		"  margin-right: 2px;"
		"  border: none;"
		"  border-top-left-radius: 4px;"
		"  border-top-right-radius: 4px;"
		"  font-size: 14px;"
		"  font-weight: bold;"
		"  border-bottom: 3px solid transparent;"
		"}"
		"QTabBar::tab:selected {"
		"  background-color: #0e639c;"
		"  color: #ffffff;"
		"  border-bottom: 3px solid #0e639c;"
		"}"
		"QTabBar::tab:hover {"
		"  background-color: #2a2d2e;"
		"  color: #d4d4d4;"
		"}"
	);

	// ============================================================================
	// TAB 1: MOTION CONTROL
	// ============================================================================
	QWidget* motionControlTab = new QWidget();
	motionControlTab->setStyleSheet("QWidget { background-color: #2d2d30; }");
	QHBoxLayout* motionContentLayout = new QHBoxLayout(motionControlTab);
	motionContentLayout->setContentsMargins(0, 0, 0, 0);
	motionContentLayout->setSpacing(0);

	// Left Panel - Motion Control (20%)
	m_controlPanel = new MotionControlPanel();
	m_controlPanel->setMinimumWidth(250);
	m_controlPanel->setMaximumWidth(400);
	motionContentLayout->addWidget(m_controlPanel, 2);

	// Middle Panel - Graphics View (expand to fill remaining space)
	m_graphicsView = new MotionGraphicsView();
	motionContentLayout->addWidget(m_graphicsView, 1);

	// ✅ REMOVED: Right Panel - Scan Control
	// Create scan panel but don't add to layout (keep for compatibility with existing code)
	m_scanPanel = new ScanControlPanel();
	m_scanPanel->hide();

	m_tabWidget->addTab(motionControlTab, "Motion Control");

	// ============================================================================
	// TAB 2: COORDINATE SYSTEMS
	// ============================================================================
	QWidget* coordinateSystemTab = new QWidget();
	coordinateSystemTab->setStyleSheet("QWidget { background-color: #2d2d30; }");
	QVBoxLayout* coordTabLayout = new QVBoxLayout(coordinateSystemTab);
	coordTabLayout->setContentsMargins(0, 0, 0, 0);

	// Create sub-tabs for Create and Test
	QTabWidget* coordSubTabs = new QTabWidget();
	coordSubTabs->setStyleSheet(
		"QTabWidget::pane {"
		"  border: 1px solid #3f3f46;"
		"  background-color: #2d2d30;"
		"}"
		"QTabBar {"
		"  background-color: #2d2d30;"
		"  border: none;"
		"}"
		"QTabBar::tab {"
		"  background-color: #1e1e1e;"
		"  color: #888888;"
		"  padding: 8px 16px;"
		"  margin-right: 2px;"
		"  border: none;"
		"  font-size: 12px;"
		"  font-weight: bold;"
		"  border-top-left-radius: 3px;"
		"  border-top-right-radius: 3px;"
		"  border-bottom: 3px solid transparent;"
		"}"
		"QTabBar::tab:selected {"
		"  background-color: #0e639c;"
		"  color: #ffffff;"
		"  font-weight: bold;"
		"  border-bottom: 3px solid #0e639c;"
		"}"
		"QTabBar::tab:hover {"
		"  background-color: #2a2d2e;"
		"  color: #d4d4d4;"
		"}"
	);

	// Create System sub-tab
	CreateSystemWidget* createSystemWidget = new CreateSystemWidget(
		m_coordinateSystemManager,
		m_motionManager,
		m_productManager,
		coordinateSystemTab
	);
	coordSubTabs->addTab(createSystemWidget, "Create System");

	// Test System sub-tab
	TestSystemWidget* testSystemWidget = new TestSystemWidget(
		m_coordinateSystemManager,
		m_motionManager,
		coordinateSystemTab
	);
	coordSubTabs->addTab(testSystemWidget, "Test System");

	// Connect signal to refresh test widget when system is created
	connect(createSystemWidget, &CreateSystemWidget::systemCreated,
		[testSystemWidget](const QString&) {
			// Refresh the system list in test widget when a new system is created
			testSystemWidget->findChild<QPushButton*>("m_refreshButton", Qt::FindChildrenRecursively);
		});

	coordTabLayout->addWidget(coordSubTabs);

	m_tabWidget->addTab(coordinateSystemTab, "Coordinate Systems");
	m_tabWidget->setTabEnabled(1, false);  // Disable Coordinate Systems tab (index 1)

	// ============================================================================
	// TAB 3: GRAPH
	// ============================================================================
	// Create placeholder - will be populated when device is selected
	QWidget* graphTab = new QWidget();
	graphTab->setStyleSheet("QWidget { background-color: #2d2d30; }");
	QVBoxLayout* graphTabLayout = new QVBoxLayout(graphTab);
	graphTabLayout->setContentsMargins(0, 0, 0, 0);

	// GraphWidget will be created in onDeviceChanged() when device is selected
	m_tabWidget->addTab(graphTab, "Graph");

	// ============================================================================
	// TAB 4: INTERLOCK
	// ============================================================================
	QWidget* interlockTab = new QWidget();
	interlockTab->setStyleSheet("QWidget { background-color: #2d2d30; }");
	QVBoxLayout* interlockTabLayout = new QVBoxLayout(interlockTab);
	interlockTabLayout->setContentsMargins(0, 0, 0, 0);
	InterlockWidget* interlockWidget = new InterlockWidget(m_motionManager, m_interlockManager, interlockTab);
	interlockTabLayout->addWidget(interlockWidget);
	m_tabWidget->addTab(interlockTab, "Interlock");

	// Add tab widget to content layout
	contentLayout->addWidget(m_tabWidget, 1);

	// Add content layout (device stack + tabs) to main layout
	mainLayout->addLayout(contentLayout);

	// Connect signals
	connect(m_backButton, &QPushButton::clicked, this, &MotionPage::backToMainMenu);

	connect(m_loadProductBtn, &QPushButton::clicked, this, &MotionPage::onLoadProductClicked);
	connect(m_saveProductBtn, &QPushButton::clicked, this, &MotionPage::onSaveProductClicked);

	// Connect device button stack (primary device selector)
	connect(m_deviceButtonStack, &DeviceButtonStack::deviceChanged,
		this, &MotionPage::onDeviceChanged);

	// Also connect control panel device selector for backward compatibility
	connect(m_controlPanel, &MotionControlPanel::deviceChanged,
		this, &MotionPage::onDeviceChanged);

	connect(m_controlPanel, &MotionControlPanel::moveToPositionClicked,
		this, &MotionPage::onMoveToPositionClicked);

	// ✅ ADD THIS LINE
	connect(m_controlPanel, &MotionControlPanel::editPositionClicked,
		this, &MotionPage::onEditPositionClicked);
	// ✅ NEW
	connect(m_controlPanel, &MotionControlPanel::moveWithOffsetClicked,
		this, &MotionPage::onMoveWithOffsetClicked);

	connect(m_controlPanel, &MotionControlPanel::updatePositionClicked,
		this, &MotionPage::onUpdatePositionClicked);

	connect(m_controlPanel, &MotionControlPanel::createPositionClicked,
		this, &MotionPage::onCreatePositionClicked);

	connect(m_controlPanel, &MotionControlPanel::deletePositionClicked,
		this, &MotionPage::onDeletePositionClicked);

	// ❌ REMOVED: Waypoint editor signal (waypoints not used)
	// connect(m_controlPanel, &MotionControlPanel::editWaypointsClicked,
	//	this, &MotionPage::onEditWaypointsClicked);

	// ✅ NEW: Auto-fit view button
	connect(m_controlPanel, &MotionControlPanel::autoFitClicked,
		this, [this]() {
		if (m_graphicsView) {
			m_graphicsView->autoFitView();
		}
	});

	connect(m_controlPanel, &MotionControlPanel::homeClicked,
		this, &MotionPage::onHomeClicked);

	connect(m_controlPanel, &MotionControlPanel::stopClicked,
		this, &MotionPage::onStopClicked);

	// Velocity preset: apply velocity based on device type (ACS per-axis, PI system)
	// Velocity controls moved to DeviceButtonStack
	connect(m_deviceButtonStack, &DeviceButtonStack::velocityPresetClicked,
		this, [this](double velocity) {
		if (m_currentDevice.isEmpty() || !m_motionManager) return;
		std::string dev = m_currentDevice.toStdString();
		std::string type = m_motionManager->GetDeviceType(dev);

		// Normalize type
		std::string upperType = type;
		for (auto& c : upperType) c = static_cast<char>(toupper(c));

		if (upperType == "PI") {
			m_motionManager->SetSystemVelocity(dev, velocity);
		} else {
			// ACS: set all installed axes
			std::string axes = m_motionManager->GetInstalledAxes(dev);
			for (char c : axes) {
				m_motionManager->SetVelocity(dev, std::string(1, c), velocity);
			}
		}
	});

	// Click-to-select removed from graphics view — selection only via position list

	// Connect position selection from list
	connect(m_controlPanel, &MotionControlPanel::positionSelectedFromList,
		this, &MotionPage::onPositionSelectedFromList);

	// Connect graph selector change
	connect(m_controlPanel, &MotionControlPanel::graphChanged,
		this, &MotionPage::loadGraphEdges);

	// ✅ REMOVED: Scan panel connections (scan panel is hidden)
	// connect(m_scanPanel, &ScanControlPanel::deviceChanged,
	// 	this, &MotionPage::onScanDeviceChanged);
	//
	// connect(m_scanPanel, &ScanControlPanel::presetChanged,
	// 	this, &MotionPage::onScanPresetChanged);
	//
	// connect(m_scanPanel, &ScanControlPanel::startScanClicked,
	// 	this, &MotionPage::onStartScanClicked);
	//
	// connect(m_scanPanel, &ScanControlPanel::stopScanClicked,
	// 	this, &MotionPage::onStopScanClicked);
	//
	// connect(m_scanPanel, &ScanControlPanel::dataChannelChanged,
	// 	this, &MotionPage::onScanDataChannelChanged);


	setStyleSheet("QWidget { background-color: #ecf0f1; }");


	// ✅ REMOVED: Scan panel preset initialization (scan panel is hidden)
	// m_scanPreset = m_scanPanel->getCurrentPreset();


}

// ============================================================================
// PRODUCT MANAGEMENT
// ============================================================================

void MotionPage::onLoadProductClicked()
{
	auto devices = m_motionManager->GetAllDeviceNames();
	for (const auto& device : devices) {
		if (m_motionManager->IsDeviceInMotion(device)) {
			QMessageBox::warning(this, "Cannot Load",
				"Stop all motion before loading new positions");
			return;
		}
	}

	// ✅ Use FileSelectorDialog - searches recursively in config folder
	QString filePath = FileSelectorDialog::getOpenFileName(
		this,
		"config",  // Folder path - base config folder
		"positions.*.json",  // File pattern - matches positions.[name].json
		"Load Positions File",  // Dialog title
		"position files",  // File type description
		true  // Recursive - searches all product subdirectories (config/[product_name]/)
	);

	if (filePath.isEmpty()) {
		return;
	}

	if (m_productManager->LoadProduct(filePath.toStdString())) {
		m_productManager->ExportToMotionManager(*m_motionManager);

		// Update label to show product name and filename
		std::string productName = m_productManager->GetCurrentProductName();
		QString filename = QFileInfo(filePath).fileName();

		m_currentProductLabel->setText(
			QString("Product: %1 | File: %2")
			.arg(QString::fromStdString(productName))
			.arg(filename)
		);
		m_saveProductBtn->setEnabled(true);

		// Save to application-wide setting (shared across all pages)
		QSettings settings("Fabrinet", "BobcatEngine");
		settings.setValue("Application/lastProductFile", filePath);

		if (!m_currentDevice.isEmpty()) {
			refreshPositionDisplay();
		}

		qDebug() << "Loaded product from:" << filePath;
		QMessageBox msgBox(this);
		msgBox.setWindowTitle("Success");
		msgBox.setText("Positions imported successfully from:\n" + filename);
		msgBox.setIcon(QMessageBox::Information);
		msgBox.setStyleSheet(DarkTheme::getStyleSheet());
		msgBox.exec();
	}
	else {
		QMessageBox msgBox(this);
		msgBox.setWindowTitle("Error");
		msgBox.setText("Failed to import positions from:\n" + filePath);
		msgBox.setIcon(QMessageBox::Critical);
		msgBox.setStyleSheet(DarkTheme::getStyleSheet());
		msgBox.exec();
	}
}

void MotionPage::onSaveProductClicked()
{
	// Import current positions from motion manager
	m_productManager->ImportFromMotionManager(*m_motionManager);

	// Save current graph if GraphWidget exists
	if (m_graphWidget) {
		m_graphWidget->saveCurrentGraph();
	}

	// If no product loaded, prompt for product name
	if (!m_productManager->HasLoadedProduct()) {
		// ✅ Create custom QInputDialog to apply dark theme
		QInputDialog inputDialog(this);
		inputDialog.setWindowTitle("Create New Product");
		inputDialog.setLabelText("Enter product name:");
		inputDialog.setTextValue("");
		inputDialog.setStyleSheet(DarkTheme::getStyleSheet());

		bool ok = (inputDialog.exec() == QDialog::Accepted);
		QString productName = inputDialog.textValue();

		if (!ok || productName.isEmpty()) {
			return;  // User cancelled
		}

		// Save as new product (creates config/productName/positions.productName.json)
		if (m_productManager->SaveProductAs(productName.toStdString())) {
			QString filePath = QString::fromStdString(m_productManager->GetCurrentProductPath());

			QMessageBox msgBox(this);
			msgBox.setWindowTitle("Success");
			msgBox.setText(QString("Positions saved to: %1").arg(filePath));
			msgBox.setIcon(QMessageBox::Information);
			msgBox.setStyleSheet(DarkTheme::getStyleSheet());
			msgBox.exec();

			// Update product label
			m_currentProductLabel->setText(
				QString("Product: %1").arg(productName)
			);

			// Save to application-wide setting
			QSettings settings("Fabrinet", "BobcatEngine");
			settings.setValue("Application/lastProductFile", filePath);
		}
		else {
			QMessageBox msgBox(this);
			msgBox.setWindowTitle("Error");
			msgBox.setText("Failed to save positions file");
			msgBox.setIcon(QMessageBox::Critical);
			msgBox.setStyleSheet(DarkTheme::getStyleSheet());
			msgBox.exec();
		}
	}
	else {
		// Product already loaded, just save to existing file
		if (m_productManager->SaveProduct()) {
			QMessageBox msgBox(this);
			msgBox.setWindowTitle("Success");
			msgBox.setText("Positions saved successfully");
			msgBox.setIcon(QMessageBox::Information);
			msgBox.setStyleSheet(DarkTheme::getStyleSheet());
			msgBox.exec();
		}
		else {
			QMessageBox msgBox(this);
			msgBox.setWindowTitle("Error");
			msgBox.setText("Failed to save positions file");
			msgBox.setIcon(QMessageBox::Critical);
			msgBox.setStyleSheet(DarkTheme::getStyleSheet());
			msgBox.exec();
		}
	}
}

// ============================================================================
// DEVICE AND POSITION MANAGEMENT
// ============================================================================



// ============================================================================
// 2. Update loadDevices() - Populate data channels
// ============================================================================
void MotionPage::loadDevices()
{
	if (!m_motionManager) {
		return;
	}

	// ✅ NEW: Refresh device button stack to show only connected devices (filtered & sorted)
	m_deviceButtonStack->refreshDevices();

	// Get all device names for control panel (backward compatibility)
	std::vector<std::string> deviceNames = m_motionManager->GetAllDeviceNames();

	QStringList devices;
	for (const auto& name : deviceNames) {
		devices << QString::fromStdString(name);
	}

	// Also populate control panel
	m_controlPanel->setDevices(devices);

	// ✅ REMOVED: Scan panel device/channel initialization (scan panel is hidden)
	// m_scanPanel->setDevices(devices);
	//
	// if (m_dataStore) {
	// 	auto channels = m_dataStore->GetAllKeys();
	// 	QStringList channelList;
	// 	for (const auto& channel : channels) {
	// 		channelList << QString::fromStdString(channel);
	// 	}
	// 	m_scanPanel->setDataChannels(channelList);
	//
	// 	if (!channelList.isEmpty()) {
	// 		m_scanDataChannel = channelList.first();
	// 	}
	// }

	// ✅ FIX: Do NOT auto-select first device on startup
	// User must explicitly select a device from the left side buttons
	// m_currentDevice remains empty until user clicks a device button
}

// ✅ NEW: Refresh device list at runtime
void MotionPage::refreshDeviceList()
{
	if (m_deviceButtonStack) {
		m_deviceButtonStack->refreshDevices();
	}
}

// ✅ NEW: Override showEvent to refresh device list when page is shown
void MotionPage::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	// Refresh device list to show only connected devices
	refreshDeviceList();

	// After refresh, check if the previously selected device is still valid
	// DeviceButtonStack::refreshDevices() restores the selection if possible,
	// but if it couldn't, we must clear MotionPage's stale state to prevent
	// moves with no valid device selected
	QString activeDevice = m_deviceButtonStack ? m_deviceButtonStack->getCurrentDevice() : QString();
	if (activeDevice.isEmpty() && !m_currentDevice.isEmpty()) {
		// Device was deselected (no longer connected or buttons were rebuilt)
		// Clear stale state to prevent operating on a non-existent device
		m_currentDevice.clear();
		m_selectedPositionName.clear();
		m_currentPositions.clear();
		if (m_controlPanel) {
			m_controlPanel->clearSelectedPosition();
			m_controlPanel->updateBreadcrumb(QString(), QString());
		}
		if (m_deviceButtonStack) {
			// Selected position display moved to graphic view overlay
		}
		if (m_graphicsView) {
			m_graphicsView->setDeviceName(QString());
		}
		refreshPositionDisplay();
	}
}

void MotionPage::loadPositionsForDevice(const QString& deviceName)
{
	m_currentDevice = deviceName;
	m_currentPositions.clear();
	m_rawPositions.clear();

	if (!m_motionManager) {
		return;
	}

	std::vector<std::string> positionNames =
		m_motionManager->GetNamedPositionList(deviceName.toStdString());

	qDebug() << "Loading positions for device:" << deviceName;

	for (const auto& posName : positionNames) {
		PositionStruct pos;
		if (m_motionManager->GetNamedPosition(deviceName.toStdString(),
			posName, pos)) {
			double globalX, globalY, globalZ;

			// ✅ FIX: For 2D graphic display, only use X and Y coordinates
			// The graphic display should not be affected by Z, U, V, W values
			if (!m_matrixLoader || m_currentDevice.isEmpty()) {
				// No transformation - use raw X, Y coordinates
				globalX = pos.x;
				globalY = pos.y;
				globalZ = pos.z;
			}
			else {
				const auto* matrix = m_matrixLoader->GetMatrix(m_currentDevice.toStdString());
				if (!matrix) {
					// No matrix available - use raw X, Y coordinates
					globalX = pos.x;
					globalY = pos.y;
					globalZ = pos.z;
				}
				else {
					// Transform X and Y using only 2D components (exclude Z)
					globalX = (*matrix)[0] * pos.x + (*matrix)[1] * pos.y;
					globalY = (*matrix)[3] * pos.x + (*matrix)[4] * pos.y;
					// Keep full 3D transformation for Z (used for color coding)
					globalZ = (*matrix)[6] * pos.x + (*matrix)[7] * pos.y + (*matrix)[8] * pos.z;
				}
			}

			QVector<double> values = {
				globalX,
				globalY,
				globalZ,
				pos.u,
				pos.v,
				pos.w
			};

			QString qPosName = QString::fromStdString(posName);
			m_currentPositions[qPosName] = values;
			m_rawPositions[qPosName] = { pos.x, pos.y, pos.z, pos.u, pos.v, pos.w };

			qDebug() << "  -" << qPosName
				<< "Local:" << pos.x << pos.y << pos.z
				<< "-> Global (2D display):" << globalX << globalY << globalZ;
		}
	}

	m_graphicsView->updatePositions(m_rawPositions);

	// Populate graph selector and load edges for the first/current graph
	if (m_productManager) {
		QList<QString> graphNames = m_productManager->GetGraphManager().getGraphNames(deviceName);
		QStringList graphNameList;
		for (const auto& name : graphNames) {
			graphNameList << name;
		}
		m_controlPanel->updateGraphList(graphNameList);
		loadGraphEdges(m_controlPanel->getCurrentGraph());
	}

	// Update position list in control panel
	QStringList positionList = m_currentPositions.keys();
	m_controlPanel->updatePositionList(positionList);

	qDebug() << "Successfully loaded" << m_currentPositions.size()
		<< "positions for" << deviceName;
}

void MotionPage::refreshPositionDisplay()
{
	if (m_currentDevice.isEmpty()) {
		return;
	}

	loadPositionsForDevice(m_currentDevice);
}

void MotionPage::loadGraphEdges(const QString& graphName)
{
	if (!m_productManager || m_currentDevice.isEmpty() || graphName.isEmpty()) {
		m_graphicsView->setGraphEdges({});
		return;
	}

	const MotionGraph* graph = m_productManager->GetGraphManager().getGraph(m_currentDevice, graphName);
	if (!graph) {
		m_graphicsView->setGraphEdges({});
		return;
	}

	QList<MotionGraphicsView::GraphEdge> edges;
	for (const MotionEdge& edge : graph->getAllEdges()) {
		const MotionNode* fromNode = graph->getNode(edge.fromNodeId);
		const MotionNode* toNode = graph->getNode(edge.toNodeId);
		if (fromNode && toNode && !fromNode->position.isEmpty() && !toNode->position.isEmpty()) {
			edges.append({fromNode->position, toNode->position});
		}
	}
	m_graphicsView->setGraphEdges(edges);
}

void MotionPage::onProductChanged()
{
	// Update product label when product changes in MainMenu
	if (m_productManager) {
		std::string productName = m_productManager->GetCurrentProductName();
		std::string productPath = m_productManager->GetCurrentProductPath();
		QString filename = QFileInfo(QString::fromStdString(productPath)).fileName();

		m_currentProductLabel->setText(
			QString("Product: %1 | File: %2")
			.arg(QString::fromStdString(productName))
			.arg(filename)
		);

		// Reload positions for current device (if any)
		if (!m_currentDevice.isEmpty()) {
			loadPositionsForDevice(m_currentDevice);
		}
	}
}

void MotionPage::transformLocalToGlobal(double localX, double localY, double localZ,
	double& globalX, double& globalY, double& globalZ)
{
	if (!m_matrixLoader || m_currentDevice.isEmpty()) {
		globalX = localX;
		globalY = localY;
		globalZ = localZ;
		return;
	}

	const auto* matrix = m_matrixLoader->GetMatrix(m_currentDevice.toStdString());

	if (!matrix) {
		qWarning() << "No transformation matrix for device:" << m_currentDevice;
		globalX = localX;
		globalY = localY;
		globalZ = localZ;
		return;
	}

	// ✅ FIX: Use 2D transformation for XY (exclude Z component) to match saved position display
	// This ensures live marker aligns with saved position markers on the 2D graphic map
	globalX = (*matrix)[0] * localX + (*matrix)[1] * localY;
	globalY = (*matrix)[3] * localX + (*matrix)[4] * localY;
	// Keep full 3D transformation for Z (used for color coding, not position)
	globalZ = (*matrix)[6] * localX + (*matrix)[7] * localY + (*matrix)[8] * localZ;
}

void MotionPage::onDeviceChanged(const QString& deviceName)
{
	// ✅ NEW: Save current device's view state before switching
	if (!m_currentDevice.isEmpty() && m_graphicsView) {
		m_graphicsView->saveViewState();
	}

	m_currentDevice = deviceName;

	// Synchronize device selection across both widgets (avoid signal loops)
	if (m_deviceButtonStack->getCurrentDevice() != deviceName) {
		m_deviceButtonStack->blockSignals(true);
		m_deviceButtonStack->setCurrentDevice(deviceName);
		m_deviceButtonStack->blockSignals(false);
	}

	// Note: We don't need to sync m_controlPanel because we're hiding its device selector

	refreshPositionDisplay();
	m_controlPanel->clearSelectedPosition();
	// Selected position display moved to graphic view overlay
	m_selectedPositionName.clear();

	// ✅ NEW: Update breadcrumb with device only (no position selected yet)
	m_controlPanel->updateBreadcrumb(m_currentDevice, QString());

	// Update velocity presets based on device type (ACS vs PI)
	if (m_motionManager) {
		std::string type = m_motionManager->GetDeviceType(deviceName.toStdString());
		for (auto& c : type) c = static_cast<char>(toupper(c));
		m_controlPanel->updateVelocityPresets(QString::fromStdString(type));
		m_deviceButtonStack->updateVelocityPresets(QString::fromStdString(type));
	}

	// ✅ NEW: Set device name and restore its saved view state
	if (m_graphicsView) {
		m_graphicsView->setDeviceName(m_currentDevice);
		m_graphicsView->restoreViewState();
	}

	// Update GraphWidget for new device
	// Find the Graph tab (index 2) and its layout
	QWidget* graphTab = m_tabWidget->widget(2);  // Graph tab is the 3rd tab (index 2)
	if (graphTab) {
		QVBoxLayout* graphLayout = qobject_cast<QVBoxLayout*>(graphTab->layout());
		if (graphLayout) {
			// Delete existing GraphWidget if any
			if (m_graphWidget) {
				graphLayout->removeWidget(m_graphWidget);
				m_graphWidget->cleanup();  // CRITICAL: Stop timer and save before deletion
				m_graphWidget->deleteLater();  // Schedule deletion
				m_graphWidget = nullptr;
			}

			// Create new GraphWidget for current device
			if (!m_currentDevice.isEmpty() && m_productManager) {
				m_graphWidget = new GraphWidget(m_currentDevice, m_productManager, graphTab);
				m_graphWidget->setMotionManager(m_motionManager);  // ✅ Connect motion manager for path execution
				m_graphWidget->setSequenceEngine(m_sequenceEngine);  // ✅ Connect sequence engine for multi-step paths
				graphLayout->addWidget(m_graphWidget);
			}
		}
	}
}

// ============================================================================
// POSITION OPERATIONS
// ============================================================================

void MotionPage::onMoveToPositionClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		QMessageBox::warning(this, "No Selection",
			"Please select a position from the map first.");
		return;
	}

	if (m_currentDevice.isEmpty()) {
		QMessageBox::warning(this, "No Device", "No device selected.");
		return;
	}

	std::cout << "[MotionPage] Moving device '" << m_currentDevice.toStdString()
		<< "' to position '" << m_selectedPositionName.toStdString() << "'" << std::endl;

	// ✅ Use SequenceEngine with MoveToNamedPosition to properly handle all 6 axes with waypoints
	if (!m_sequenceEngine) {
		QMessageBox::critical(this, "Error", "Sequence engine not available.");
		return;
	}

	// Create a single-command sequence
	SequenceBuilder* sequence = m_sequenceEngine->CreateSequence("MoveToPosition_" + m_selectedPositionName.toStdString());

	// Add MoveToNamedPosition action
	std::map<std::string, std::string> params;
	params["positionName"] = m_selectedPositionName.toStdString();
	params["blocking"] = "false";  // Non-blocking execution

	sequence->AddAction(m_currentDevice.toStdString(), "MoveToNamedPosition", params);

	// Execute sequence on worker thread
	SequenceContext* context = new SequenceContext();
	m_sequenceEngine->ClearStopFlag();

	bool success = m_sequenceEngine->ExecuteSequence(sequence, context);

	if (!success) {
		// ✅ Apply dark theme to error dialog
		QMessageBox msgBox(this);
		msgBox.setStyleSheet(DarkTheme::getStyleSheet());
		msgBox.setIcon(QMessageBox::Critical);
		msgBox.setWindowTitle("Motion Error");
		msgBox.setText("Failed to start motion to position");
		msgBox.exec();
	}

	delete context;
	delete sequence;
}

void MotionPage::onUpdatePositionClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		QMessageBox::warning(this, "No Selection",
			"Please select a position from the map first.");
		return;
	}

	double x, y, z, u, v, w;
	if (!getCurrentDevicePosition(x, y, z, u, v, w)) {
		QMessageBox::critical(this, "Error",
			"Failed to get current device position.");
		return;
	}

	// ✅ Create custom QMessageBox to apply dark theme
	QMessageBox msgBox(this);
	msgBox.setWindowTitle("Update Position");
	msgBox.setText(QString("Update saved position '%1' with current device position?\n\n"
			"Current: X=%2, Y=%3, Z=%4\n"
			"         U=%5, V=%6, W=%7")
		.arg(m_selectedPositionName)
		.arg(x, 0, 'f', 3)
		.arg(y, 0, 'f', 3)
		.arg(z, 0, 'f', 3)
		.arg(u, 0, 'f', 3)
		.arg(v, 0, 'f', 3)
		.arg(w, 0, 'f', 3));
	msgBox.setIcon(QMessageBox::Question);
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	msgBox.setStyleSheet(DarkTheme::getStyleSheet());

	QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

	if (reply == QMessageBox::Yes) {
		PositionStruct existingPos;
		if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos)) {
			existingPos = PositionStruct();
		}

		existingPos.x = x;
		existingPos.y = y;
		existingPos.z = z;
		existingPos.u = u;
		existingPos.v = v;
		existingPos.w = w;

		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos);

		// ✅ AUTO-SAVE: Automatically save positions after updating position
		m_productManager->ImportFromMotionManager(*m_motionManager);

		// If product is already loaded, save to existing file
		if (m_productManager->HasLoadedProduct()) {
			if (m_productManager->SaveProduct()) {
				QMessageBox saveSuccessBox(this);
				saveSuccessBox.setWindowTitle("Success");
				saveSuccessBox.setText("Position updated and saved successfully!");
				saveSuccessBox.setIcon(QMessageBox::Information);
				saveSuccessBox.setStyleSheet(DarkTheme::getStyleSheet());
				saveSuccessBox.exec();
			}
			else {
				QMessageBox saveFailBox(this);
				saveFailBox.setWindowTitle("Position Updated");
				saveFailBox.setText("Position updated but failed to auto-save.\n"
					"Please use 'Save Positions' button manually.");
				saveFailBox.setIcon(QMessageBox::Warning);
				saveFailBox.setStyleSheet(DarkTheme::getStyleSheet());
				saveFailBox.exec();
			}
		}
		else {
			// No product loaded - show success but remind to save
			QMessageBox noProductBox(this);
			noProductBox.setWindowTitle("Position Updated");
			noProductBox.setText("Position updated successfully!\n\n"
				"Click 'Save Positions' to save to a file.");
			noProductBox.setIcon(QMessageBox::Information);
			noProductBox.setStyleSheet(DarkTheme::getStyleSheet());
			noProductBox.exec();
		}

		refreshPositionDisplay();
	}
}

void MotionPage::onCreatePositionClicked()
{
	double x, y, z, u, v, w;
	if (!getCurrentDevicePosition(x, y, z, u, v, w)) {
		QMessageBox::critical(this, "Error",
			"Failed to get current device position.");
		return;
	}

	NewPositionDialog dialog(m_currentDevice, x, y, z, u, v, w, this);

	if (dialog.exec() == QDialog::Accepted) {
		QString name = dialog.getPositionName();

		if (m_currentPositions.contains(name)) {
			QMessageBox msgBox(this);
			msgBox.setWindowTitle("Position Exists");
			msgBox.setText(QString("Position '%1' already exists. "
					"Please use a different name.").arg(name));
			msgBox.setIcon(QMessageBox::Warning);
			msgBox.setStyleSheet(DarkTheme::getStyleSheet());
			msgBox.exec();
			return;
		}

		PositionStruct pos;
		pos.x = dialog.getX();
		pos.y = dialog.getY();
		pos.z = dialog.getZ();
		pos.u = dialog.getU();
		pos.v = dialog.getV();
		pos.w = dialog.getW();

		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			name.toStdString(),
			pos);

		// ✅ AUTO-SAVE: Automatically save positions after creating new position
		m_productManager->ImportFromMotionManager(*m_motionManager);

		// If product is already loaded, save to existing file
		if (m_productManager->HasLoadedProduct()) {
			if (m_productManager->SaveProduct()) {
				QMessageBox createSuccessBox(this);
				createSuccessBox.setWindowTitle("Success");
				createSuccessBox.setText(QString("Position '%1' created and saved successfully!").arg(name));
				createSuccessBox.setIcon(QMessageBox::Information);
				createSuccessBox.setStyleSheet(DarkTheme::getStyleSheet());
				createSuccessBox.exec();
			}
			else {
				QMessageBox createFailBox(this);
				createFailBox.setWindowTitle("Position Created");
				createFailBox.setText(QString("Position '%1' created but failed to auto-save.\n"
						"Please use 'Save Positions' button manually.").arg(name));
				createFailBox.setIcon(QMessageBox::Warning);
				createFailBox.setStyleSheet(DarkTheme::getStyleSheet());
				createFailBox.exec();
			}
		}
		else {
			// No product loaded - show success but remind to save
			QMessageBox createNoProductBox(this);
			createNoProductBox.setWindowTitle("Position Created");
			createNoProductBox.setText(QString("Position '%1' created successfully!\n\n"
					"Click 'Save Positions' to save to a file.").arg(name));
			createNoProductBox.setIcon(QMessageBox::Information);
			createNoProductBox.setStyleSheet(DarkTheme::getStyleSheet());
			createNoProductBox.exec();
		}

		refreshPositionDisplay();
	}
}

void MotionPage::onDeletePositionClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		QMessageBox::warning(this, "No Selection",
			"Please select a position to delete.");
		return;
	}

	// ✅ Create custom QMessageBox to apply dark theme
	QMessageBox msgBox(this);
	msgBox.setWindowTitle("Delete Position");
	msgBox.setText(QString("Are you sure you want to delete position '%1'?\n\n"
			"This will automatically save the product.")
		.arg(m_selectedPositionName));
	msgBox.setIcon(QMessageBox::Question);
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	msgBox.setStyleSheet(DarkTheme::getStyleSheet());

	QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

	if (reply == QMessageBox::Yes) {
		if (m_motionManager->RemoveNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString())) {

			m_selectedPositionName.clear();
			m_controlPanel->clearSelectedPosition();

			refreshPositionDisplay();

			if (m_productManager->HasLoadedProduct()) {
				m_productManager->ImportFromMotionManager(*m_motionManager);

				if (m_productManager->SaveProduct()) {
					QMessageBox deleteSuccessBox(this);
					deleteSuccessBox.setWindowTitle("Success");
					deleteSuccessBox.setText("Position deleted and product saved successfully!");
					deleteSuccessBox.setIcon(QMessageBox::Information);
					deleteSuccessBox.setStyleSheet(DarkTheme::getStyleSheet());
					deleteSuccessBox.exec();
				}
				else {
					QMessageBox deletePartialBox(this);
					deletePartialBox.setWindowTitle("Partial Success");
					deletePartialBox.setText("Position deleted but failed to save product file.\n"
						"Please save manually.");
					deletePartialBox.setIcon(QMessageBox::Warning);
					deletePartialBox.setStyleSheet(DarkTheme::getStyleSheet());
					deletePartialBox.exec();
				}
			}
			else {
				QMessageBox deleteNoProductBox(this);
				deleteNoProductBox.setWindowTitle("Success");
				deleteNoProductBox.setText("Position deleted successfully!\n\n"
					"No product loaded - remember to save when ready.");
				deleteNoProductBox.setIcon(QMessageBox::Information);
				deleteNoProductBox.setStyleSheet(DarkTheme::getStyleSheet());
				deleteNoProductBox.exec();
			}
		}
		else {
			QMessageBox deleteErrorBox(this);
			deleteErrorBox.setWindowTitle("Error");
			deleteErrorBox.setText("Failed to delete position.");
			deleteErrorBox.setIcon(QMessageBox::Critical);
			deleteErrorBox.setStyleSheet(DarkTheme::getStyleSheet());
			deleteErrorBox.exec();
		}
	}
}

void MotionPage::onHomeClicked()
{
	if (m_currentDevice.isEmpty()) {
		return;
	}

	// ✅ Create custom QMessageBox to apply dark theme
	QMessageBox msgBox(this);
	msgBox.setWindowTitle("Home Device");
	msgBox.setText(QString("Send device '%1' to home position?").arg(m_currentDevice));
	msgBox.setIcon(QMessageBox::Question);
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	msgBox.setStyleSheet(DarkTheme::getStyleSheet());

	QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

	if (reply == QMessageBox::Yes) {
		bool success = m_motionManager->MoveToNamedPosition(
			m_currentDevice.toStdString(),
			"home",
			false  // Non-blocking
		);

		if (!success) {
			QMessageBox::critical(this, "Error", "Failed to home device.");
		}
	}
}

void MotionPage::onStopClicked()
{
	if (m_currentDevice.isEmpty()) {
		(void)m_motionManager->EmergencyStopAll();
	}
	else {
		// Cancel waypoint sequence and emergency stop
		m_motionManager->CancelWaypointSequence(m_currentDevice.toStdString());
	}

	// Create styled message box
	QMessageBox msgBox(this);
	msgBox.setIcon(QMessageBox::Information);
	msgBox.setWindowTitle("Stopped");
	msgBox.setText("Device motion and waypoint sequence stopped.");
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setStyleSheet(DarkTheme::getStyleSheet());
	msgBox.exec();
}

void MotionPage::onPositionSelected(const QString& name, double /*x*/, double /*y*/, double /*z*/)
{
	m_selectedPositionName = name;

	if (m_currentPositions.contains(name)) {
		QVector<double> pos = m_currentPositions[name];

		// ✅ Get current position from UnifiedMotionManager
		std::string currentPosName = m_motionManager->GetCurrentPositionName(
			m_currentDevice.toStdString());

		std::vector<std::string> entryWaypoints;
		std::vector<std::string> exitWaypoints;

		PositionStruct selectedPos;
		if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			name.toStdString(),
			selectedPos)) {

			entryWaypoints = selectedPos.waypoints;
			exitWaypoints = selectedPos.exit_waypoints;
		}

		// Get exit waypoints from current position
		std::vector<std::string> currentExitWaypoints;
		if (!currentPosName.empty() && currentPosName != name.toStdString()) {
			PositionStruct currentPos;
			if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
				currentPosName,
				currentPos)) {
				currentExitWaypoints = currentPos.exit_waypoints;
			}
		}

		// Use raw axis values for display (not transformed global)
		QVector<double> raw = m_rawPositions.value(name, pos);

		// Update both control panel and device button stack with raw position info
		m_controlPanel->setSelectedPosition(name,
			raw[0], raw[1], raw[2],
			raw[3], raw[4], raw[5],
			entryWaypoints);

		// Selected position display moved to graphic view overlay

		// Update graphic view overlay with raw axis values
		m_graphicsView->setSelectedPositionInfo(name,
			raw[0], raw[1], raw[2],
			raw[3], raw[4], raw[5]);

		// ✅ NEW: Also select in position list
		m_controlPanel->selectPositionInList(name);

		// ✅ NEW: Update breadcrumb with device and position
		m_controlPanel->updateBreadcrumb(m_currentDevice, name);

		// Path preview removed — graph edges show connectivity

		// Highlight connected graph edges
		m_graphicsView->highlightEdgesForPosition(name);
	}
}

void MotionPage::onPositionDeselected()
{
	m_selectedPositionName.clear();
	m_controlPanel->clearSelectedPosition();
	// Selected position display moved to graphic view overlay
	m_graphicsView->clearSelectedPositionInfo();
	// Path preview removed — graph edges show connectivity
	m_graphicsView->clearEdgeHighlight();

	// ✅ NEW: Update breadcrumb to show only device (no position)
	m_controlPanel->updateBreadcrumb(m_currentDevice, QString());
}

// ✅ NEW: Handle position selection from list
void MotionPage::onPositionSelectedFromList(const QString& name)
{
	// Get position data
	if (!m_currentPositions.contains(name)) {
		qWarning() << "Position" << name << "not found in current positions";
		return;
	}

	const QVector<double>& pos = m_currentPositions[name];
	if (pos.size() < 6) {
		qWarning() << "Invalid position data for" << name;
		return;
	}

	// Update selected position name
	m_selectedPositionName = name;

	// Get waypoints from the position structure
	std::vector<std::string> entryWaypoints;
	std::vector<std::string> exitWaypoints;
	PositionStruct selectedPos;
	if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
		name.toStdString(),
		selectedPos)) {
		entryWaypoints = selectedPos.waypoints;
		exitWaypoints = selectedPos.exit_waypoints;
	}

	// Get exit waypoints from current position
	std::string currentPosName = m_motionManager->GetCurrentPositionName(
		m_currentDevice.toStdString());
	std::vector<std::string> currentExitWaypoints;
	if (!currentPosName.empty() && currentPosName != name.toStdString()) {
		PositionStruct currentPos;
		if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			currentPosName,
			currentPos)) {
			currentExitWaypoints = currentPos.exit_waypoints;
		}
	}

	// Use raw axis values for display (not transformed global)
	QVector<double> raw = m_rawPositions.value(name, pos);

	// Update control panel with raw values
	m_controlPanel->setSelectedPosition(name, raw[0], raw[1], raw[2],
		raw[3], raw[4], raw[5], entryWaypoints);

	// Update graphic view overlay with raw axis values
	m_graphicsView->setSelectedPositionInfo(name,
		raw[0], raw[1], raw[2],
		raw[3], raw[4], raw[5]);

	// ✅ NEW: Update breadcrumb with device and position
	m_controlPanel->updateBreadcrumb(m_currentDevice, name);

	// Highlight connected graph edges
	m_graphicsView->highlightEdgesForPosition(name);

	// Center on position with high zoom (2300% = zoom level 23.0)
	m_graphicsView->centerOnPosition(name, 23.0);

	qDebug() << "[MotionPage] Position selected from list:" << name;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool MotionPage::getCurrentDevicePosition(double& x, double& y, double& z,
	double& u, double& v, double& w)
{
	if (!m_motionManager || m_currentDevice.isEmpty()) {
		return false;
	}

	PositionStruct pos = m_motionManager->GetCurrentPosition(
		m_currentDevice.toStdString()
	);

	x = pos.x;
	y = pos.y;
	z = pos.z;
	u = pos.u;
	v = pos.v;
	w = pos.w;

	return true;
}

void MotionPage::onUpdateLivePosition()
{
	if (m_currentDevice.isEmpty() || !m_motionManager) {
		m_graphicsView->showLiveMarker(false);
		return;
	}

	double localX, localY, localZ, u, v, w;
	if (getCurrentDevicePosition(localX, localY, localZ, u, v, w)) {
		// Use raw local coordinates to match isometric view
		m_graphicsView->updateLiveMarker(m_currentDevice, localX, localY, localZ);
		m_graphicsView->setLivePositionInfo(localX, localY, localZ, u, v, w);
	}
	else {
		m_graphicsView->showLiveMarker(false);
	}
}

// ============================================================================
// LABEL OFFSET MANAGEMENT
// ============================================================================

// ============================================================================
// WAYPOINT EDITOR
// ============================================================================

// ❌ REMOVED: Waypoint editor handler (waypoints not used)
// void MotionPage::onEditWaypointsClicked()
// {
// 	if (m_selectedPositionName.isEmpty()) {
// 		return;
// 	}
//
// 	PositionStruct currentPos;
// 	if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
// 		m_selectedPositionName.toStdString(),
// 		currentPos)) {
// 		QMessageBox::critical(this, "Error", "Failed to get position data");
// 		return;
// 	}
//
// 	std::vector<std::string> availablePositions =
// 		m_motionManager->GetNamedPositionList(m_currentDevice.toStdString());
//
// 	WaypointEditorDialog dialog(m_selectedPositionName,
// 		currentPos.waypoints,
// 		currentPos.exit_waypoints,
// 		availablePositions,
// 		this);
//
// 	if (dialog.exec() == QDialog::Accepted) {
// 		currentPos.waypoints = dialog.getWaypoints();
// 		currentPos.exit_waypoints = dialog.getExitWaypoints();
//
// 		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
// 			m_selectedPositionName.toStdString(),
// 			currentPos);
//
// 		if (m_productManager->HasLoadedProduct()) {
// 			m_productManager->ImportFromMotionManager(*m_motionManager);
// 			m_productManager->SaveProduct();
// 		}
//
// 		refreshPositionDisplay();
//
// 		m_graphicsView->showPathPreview(m_selectedPositionName,
// 			currentPos.waypoints,
// 			currentPos.exit_waypoints);
//
// 		QMessageBox::information(this, "Success",
// 			"Waypoints updated successfully!");
// 	}
// }

void MotionPage::onMoveWithOffsetClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		QMessageBox::warning(this, "No Selection",
			"Please select a position from the map first.");
		return;
	}

	if (m_currentDevice.isEmpty()) {
		QMessageBox::warning(this, "No Device", "No device selected.");
		return;
	}

	// Get position values for dialog
	if (!m_currentPositions.contains(m_selectedPositionName)) {
		QMessageBox::critical(this, "Error", "Position data not found.");
		return;
	}

	QVector<double> pos = m_currentPositions[m_selectedPositionName];

	// Show offset dialog
	OffsetMoveDialog dialog(m_selectedPositionName, pos[0], pos[1], pos[2], this);

	if (dialog.exec() == QDialog::Accepted) {
		QString offsetPosName = dialog.getOffsetPositionName();

		std::cout << "[MotionPage] Moving device '" << m_currentDevice.toStdString()
			<< "' to calculated position '" << offsetPosName.toStdString() << "'" << std::endl;

		// ✅ Move using calculated position name
		bool success = m_motionManager->MoveToNamedPosition(
			m_currentDevice.toStdString(),
			offsetPosName.toStdString(),
			false  // Non-blocking
		);

		if (!success) {
			QMessageBox::critical(this, "Motion Error",
				"Failed to start motion to offset position");
		}
	}
}



// ============================================================================
// 5. ADD NEW SCAN CONTROL SLOT IMPLEMENTATIONS
// ============================================================================


// ============================================================================
// CREATE SCAN CONTROLLER
// ============================================================================
void MotionPage::createScanController()
{
	if (m_scanDevice.isEmpty()) {
		QMessageBox::warning(this, "No Device", "Please select a device for scanning.");
		return;
	}

	// Create controller
	m_scanController = std::make_unique<AdaptiveScanController>(
		*m_motionManager,
		*m_dataStore,
		m_scanDevice.toStdString(),
		m_scanDataChannel.toStdString()  // ✅ Use selected channel
	);

	// Set up callbacks
	m_scanController->SetProgressCallback(
		[this](const ScanProgressEvent& event) {
		QMetaObject::invokeMethod(this, [this, event]() {
			onScanProgress(event);
		}, Qt::QueuedConnection);
	}
	);

	m_scanController->SetDataPointCallback(
		[this](const ScanDataPoint& dataPoint) {
		QMetaObject::invokeMethod(this, [this, dataPoint]() {
			onScanDataPoint(dataPoint);
		}, Qt::QueuedConnection);
	}
	);

	m_scanController->SetPeakFoundCallback(
		[this](const PeakInfo& peak) {
		QMetaObject::invokeMethod(this, [this, peak]() {
			onScanPeakFound(peak);
		}, Qt::QueuedConnection);
	}
	);

	m_scanController->SetCompletionCallback(
		[this](const ScanResults& results) {
		QMetaObject::invokeMethod(this, [this, results]() {
			onScanCompleted(results);
		}, Qt::QueuedConnection);
	}
	);

	m_scanController->SetErrorCallback(
		[this](const ScanError& error) {
		QMetaObject::invokeMethod(this, [this, error]() {
			onScanError(error);
		}, Qt::QueuedConnection);
	}
	);
}

// ============================================================================
// CREATE SCAN PARAMETERS BASED ON PRESET
// ============================================================================
ScanParameters MotionPage::createScanParameters(const QString& preset)
{
	ScanParameters params;

	if (preset == "Coarse") {
		// Coarse scan - larger steps, faster
		params.axesToScan = { "Z", "X", "Y" };
		params.stepSizes = { 0.010, 0.020 };  // 10, 20 microns
		params.motionSettleTimeMs = 300;
		params.velocityPercent = 60.0;
		params.consecutiveDecreasesLimit = 3;
		params.significantDecreaseThreshold = 0.10;  // 10%
		params.maxTotalDistance = 5.0;  // mm
		params.improvementThreshold = 0.02;  // 2%
		params.averagingCount = 3;
	}
	else if (preset == "Fine") {
		// Fine scan - smaller steps, more precise
		params.axesToScan = { "Z", "X", "Y" };
		params.stepSizes = { 0.001, 0.005, 0.010 };  // 1, 5, 10 microns
		params.motionSettleTimeMs = 400;
		params.velocityPercent = 50.0;
		params.consecutiveDecreasesLimit = 3;
		params.significantDecreaseThreshold = 0.05;  // 5%
		params.maxTotalDistance = 3.0;  // mm
		params.improvementThreshold = 0.01;  // 1%
		params.averagingCount = 5;
	}
	else {
		// Default to fine
		params.axesToScan = { "Z", "X", "Y" };
		params.stepSizes = { 0.001, 0.005, 0.010 };
		params.motionSettleTimeMs = 400;
		params.velocityPercent = 50.0;
		params.consecutiveDecreasesLimit = 3;
		params.significantDecreaseThreshold = 0.05;
		params.maxTotalDistance = 3.0;
		params.improvementThreshold = 0.01;
		params.averagingCount = 5;
	}

	return params;
}



void MotionPage::onScanDeviceChanged(const QString& deviceName)
{
	m_scanDevice = deviceName;
	qDebug() << "Scan device changed to:" << deviceName;
}

void MotionPage::onScanPresetChanged(const QString& preset)
{
	m_scanPreset = preset;
	qDebug() << "Scan preset changed to:" << preset;
}

// ============================================================================
// START SCAN - REPLACE PLACEHOLDER
// ============================================================================
void MotionPage::onStartScanClicked()
{
	if (m_scanDevice.isEmpty()) {
		QMessageBox::warning(this, "No Device", "Please select a device for scanning.");
		return;
	}

	if (m_scanPreset.isEmpty()) {
		QMessageBox::warning(this, "No Preset", "Please select a scan preset.");
		return;
	}

	// Check if device is in motion
	if (m_motionManager->IsDeviceInMotion(m_scanDevice.toStdString())) {
		QMessageBox::warning(this, "Device Busy",
			"Device is currently in motion. Please wait or stop the motion first.");
		return;
	}

	qDebug() << "Starting" << m_scanPreset << "scan on device:" << m_scanDevice;

	// Create scan controller if needed
	if (!m_scanController) {
		createScanController();
	}

	// Create scan parameters based on preset
	ScanParameters params = createScanParameters(m_scanPreset);

	// Validate parameters
	std::string validationError;
	if (!params.Validate(validationError)) {
		QMessageBox::critical(this, "Invalid Parameters",
			QString("Scan parameters are invalid:\n%1")
			.arg(QString::fromStdString(validationError)));
		return;
	}

	// Start the scan
	bool success = m_scanController->StartScan(params);

	if (success) {
		m_scanPanel->setScanActive(true);
		m_scanPanel->setStatus("Initializing...");
		m_scanPanel->setProgress(0.0);

		qDebug() << "Scan started successfully";
	}
	else {
		QMessageBox::critical(this, "Scan Failed",
			"Failed to start scan. Check that device is ready and not already scanning.");

		m_scanPanel->setStatus("Failed to start");
	}
}



// ============================================================================
// STOP SCAN - REPLACE PLACEHOLDER
// ============================================================================
void MotionPage::onStopScanClicked()
{
	qDebug() << "Stopping scan...";

	if (m_scanController) {
		m_scanController->HaltScan();
	}

	m_scanPanel->setScanActive(false);
	m_scanPanel->setStatus("Stopped");
	m_scanPanel->setProgress(-1);
}



// ============================================================================
// SCAN CALLBACK IMPLEMENTATIONS
// ============================================================================

void MotionPage::onScanProgress(const ScanProgressEvent& event)
{
	m_scanPanel->setProgress(event.progress);
	m_scanPanel->setStatus(QString::fromStdString(event.message));

	qDebug() << "Scan progress:" << (event.progress * 100) << "%"
		<< "-" << QString::fromStdString(event.message);
}

void MotionPage::onScanDataPoint(const ScanDataPoint& dataPoint)
{
	// Optional: Visualize data points on the graphics view
	qDebug() << "Data point:" << dataPoint.value
		<< "at (" << dataPoint.position.x << ","
		<< dataPoint.position.y << "," << dataPoint.position.z << ")";
}

void MotionPage::onScanPeakFound(const PeakInfo& peak)
{
	qDebug() << "New peak found! Value:" << peak.value
		<< "at (" << peak.position.x << ","
		<< peak.position.y << "," << peak.position.z << ")";

	m_scanPanel->setStatus(QString("Peak: %1").arg(peak.value, 0, 'f', 6));
}

void MotionPage::onScanCompleted(const ScanResults& results)
{
	qDebug() << "Scan completed!";
	qDebug() << "  Total measurements:" << results.totalMeasurements;
	qDebug() << "  Peak value:" << results.globalPeak.value;
	qDebug() << "  Peak position: ("
		<< results.globalPeak.position.x << ","
		<< results.globalPeak.position.y << ","
		<< results.globalPeak.position.z << ")";

	m_scanPanel->setScanActive(false);
	m_scanPanel->setStatus("Completed");
	m_scanPanel->setProgress(1.0);

	// Show results dialog
	QString message = QString(
		"Scan completed successfully!\n\n"
		"Total measurements: %1\n"
		"Peak value: %2\n"
		"Peak position:\n"
		"  X: %3 mm\n"
		"  Y: %4 mm\n"
		"  Z: %5 mm\n\n"
		"Results saved to logs/scanning/"
	).arg(results.totalMeasurements)
		.arg(results.globalPeak.value, 0, 'f', 6)
		.arg(results.globalPeak.position.x, 0, 'f', 3)
		.arg(results.globalPeak.position.y, 0, 'f', 3)
		.arg(results.globalPeak.position.z, 0, 'f', 3);

	QMessageBox::information(this, "Scan Complete", message);

	// ✅ Create custom QMessageBox to apply dark theme
	QMessageBox msgBox(this);
	msgBox.setWindowTitle("Save Peak Position");
	msgBox.setText("Would you like to save the peak position as a named position?");
	msgBox.setIcon(QMessageBox::Question);
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	msgBox.setStyleSheet(DarkTheme::getStyleSheet());

	QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

	if (reply == QMessageBox::Yes) {
		// ✅ Create custom QInputDialog to apply dark theme
		QInputDialog inputDialog(this);
		inputDialog.setWindowTitle("Save Peak Position");
		inputDialog.setLabelText("Enter position name:");
		inputDialog.setTextValue(QString("peak_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
		inputDialog.setStyleSheet(DarkTheme::getStyleSheet());

		bool ok = (inputDialog.exec() == QDialog::Accepted);
		QString name = inputDialog.textValue();

		if (ok && !name.isEmpty()) {
			PositionStruct pos;
			pos.x = results.globalPeak.position.x;
			pos.y = results.globalPeak.position.y;
			pos.z = results.globalPeak.position.z;
			pos.u = 0.0;
			pos.v = 0.0;
			pos.w = 0.0;

			m_motionManager->AddNamedPosition(
				m_scanDevice.toStdString(),
				name.toStdString(),
				pos
			);

			refreshPositionDisplay();

			QMessageBox::information(this, "Saved",
				QString("Peak position saved as '%1'").arg(name));
		}
	}

	// Reset status after delay
	QTimer::singleShot(3000, this, [this]() {
		m_scanPanel->setStatus("Idle");
		m_scanPanel->setProgress(-1);
	});
}

void MotionPage::onScanError(const ScanError& error)
{
	qDebug() << "Scan error:" << QString::fromStdString(error.message);

	m_scanPanel->setScanActive(false);
	m_scanPanel->setStatus("Error");
	m_scanPanel->setProgress(-1);

	QMessageBox::critical(this, "Scan Error",
		QString("Scan failed:\n%1").arg(QString::fromStdString(error.message)));

	// Reset status after delay
	QTimer::singleShot(3000, this, [this]() {
		m_scanPanel->setStatus("Idle");
	});
}

// ============================================================================
// 3. Add new slot implementation
// ============================================================================
void MotionPage::onScanDataChannelChanged(const QString& channel)
{
	m_scanDataChannel = channel;
	qDebug() << "Scan data channel changed to:" << channel;
}



// Add this slot implementation (put it near onUpdatePositionClicked):

void MotionPage::onEditPositionClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		QMessageBox::warning(this, "No Selection",
			"Please select a position from the map first.");
		return;
	}

	// ✅ FIX: Get position data from motion manager (local coordinates), NOT from m_currentPositions (transformed coordinates)
	PositionStruct pos;
	if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
		m_selectedPositionName.toStdString(),
		pos)) {
		QMessageBox::critical(this, "Error", "Failed to get position data from motion manager.");
		return;
	}

	// Show edit dialog with LOCAL coordinates (not transformed)
	EditPositionDialog dialog(m_selectedPositionName,
		pos.x, pos.y, pos.z,  // X, Y, Z (local coordinates)
		pos.u, pos.v, pos.w,  // U, V, W
		this);

	if (dialog.exec() == QDialog::Accepted) {
		// Get the position structure to preserve waypoints
		PositionStruct existingPos;
		if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos)) {
			existingPos = PositionStruct();
		}

		// Update only XYZ UVW values
		existingPos.x = dialog.getX();
		existingPos.y = dialog.getY();
		existingPos.z = dialog.getZ();
		existingPos.u = dialog.getU();
		existingPos.v = dialog.getV();
		existingPos.w = dialog.getW();

		// Save back to motion manager
		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos);

		QMessageBox msgBox(this);
		msgBox.setWindowTitle("Success");
		msgBox.setText("Position values updated successfully!\n\nRemember to save the product file.");
		msgBox.setIcon(QMessageBox::Information);
		msgBox.setStyleSheet(DarkTheme::getStyleSheet());
		msgBox.exec();

		// Refresh display
		refreshPositionDisplay();

		// Re-select to update panel display
		onPositionSelected(m_selectedPositionName,
			existingPos.x, existingPos.y, existingPos.z);
	}
}