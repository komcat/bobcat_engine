// MotionPage.cpp
#include "MotionPage.h"
#include "NewPositionDialog.h"
#include "WaypointEditorDialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QDebug>
#include <QJsonDocument>  // ✅ Add this
#include <QJsonObject>    // ✅ Add this
#include <QJsonArray>     // ✅ Add this (if needed)
#include <QFile>          // ✅ Add this

MotionPage::MotionPage(UnifiedMotionManager* motionManager,
	ProductPositionManager* productManager,
	TransformationMatrixLoader* matrixLoader,
	QWidget* parent)
	: QWidget(parent)
	, m_motionManager(motionManager)
	, m_productManager(productManager)
	, m_matrixLoader(matrixLoader)
	, m_controlPanel(nullptr)
	, m_graphicsView(nullptr)
	, m_livePositionTimer(nullptr)
	, m_labelOffsetsFilePath("config/position_labels.json")
{

	// ✅ ADD THIS DEBUG CHECK
	if (!m_productManager) {
		std::cout << "[ERROR] MotionPage: ProductPositionManager is NULL!" << std::endl;
	}
	else {
		std::cout << "[INFO] MotionPage: ProductPositionManager at: "
			<< (void*)m_productManager << std::endl;

		// Test if it's initialized properly
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
	loadLabelOffsets();

	// Try to auto-load last used product
	QSettings settings;
	QString lastProduct = settings.value("lastProductFile").toString();
	if (!lastProduct.isEmpty() && QFile::exists(lastProduct)) {
		if (m_productManager->LoadProduct(lastProduct.toStdString())) {
			m_productManager->ExportToMotionManager(*m_motionManager);
			m_currentProductLabel->setText("Product: " +
				QString::fromStdString(m_productManager->GetCurrentProductName()));

			if (!m_currentDevice.isEmpty()) {
				refreshPositionDisplay();
			}
		}
	}

	// Setup live position polling timer
	m_livePositionTimer = new QTimer(this);
	m_livePositionTimer->setInterval(100);
	connect(m_livePositionTimer, &QTimer::timeout, this, &MotionPage::onUpdateLivePosition);
	m_livePositionTimer->start();

	// Connect label offset changes to save
	connect(m_graphicsView, &MotionGraphicsView::labelOffsetChanged,
		this, &MotionPage::onLabelOffsetChanged);
}

void MotionPage::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Top bar with back button
	QWidget* topBar = new QWidget();
	topBar->setStyleSheet("QWidget { background-color: white; border-bottom: 2px solid #bdc3c7; }");
	topBar->setFixedHeight(60);

	QHBoxLayout* topLayout = new QHBoxLayout(topBar);
	topLayout->setContentsMargins(15, 10, 15, 10);

	// Back button
	m_backButton = new QPushButton("← Back to Main Menu");
	m_backButton->setFixedHeight(40);
	m_backButton->setStyleSheet(
		"QPushButton {"
		"  background-color: #95a5a6;"
		"  color: white;"
		"  border: 2px solid #7f8c8d;"
		"  border-radius: 6px;"
		"  font-size: 14px;"
		"  font-weight: bold;"
		"  padding: 8px 20px;"
		"}"
		"QPushButton:hover {"
		"  background-color: #7f8c8d;"
		"}"
		"QPushButton:pressed {"
		"  background-color: #6c7a89;"
		"}"
	);
	topLayout->addWidget(m_backButton);

	// Title
	QLabel* titleLabel = new QLabel("Motion Control");
	titleLabel->setStyleSheet(
		"QLabel {"
		"  color: #2c3e50;"
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
		"  color: #34495e;"
		"  font-size: 12px;"
		"  font-weight: bold;"
		"  padding: 5px;"
		"}"
	);
	productLayout->addWidget(m_currentProductLabel);

	m_loadProductBtn = new QPushButton("Load Product");
	m_loadProductBtn->setFixedHeight(35);
	m_loadProductBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #3498db;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 4px;"
		"  font-size: 12px;"
		"  padding: 5px 15px;"
		"}"
		"QPushButton:hover {"
		"  background-color: #2980b9;"
		"}"
	);
	productLayout->addWidget(m_loadProductBtn);

	m_saveProductBtn = new QPushButton("Save");
	m_saveProductBtn->setFixedHeight(35);
	m_saveProductBtn->setEnabled(false);
	m_saveProductBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #27ae60;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 4px;"
		"  font-size: 12px;"
		"  padding: 5px 15px;"
		"}"
		"QPushButton:hover {"
		"  background-color: #229954;"
		"}"
		"QPushButton:disabled {"
		"  background-color: #95a5a6;"
		"}"
	);
	productLayout->addWidget(m_saveProductBtn);

	m_saveProductAsBtn = new QPushButton("Save As...");
	m_saveProductAsBtn->setFixedHeight(35);
	m_saveProductAsBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #16a085;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 4px;"
		"  font-size: 12px;"
		"  padding: 5px 15px;"
		"}"
		"QPushButton:hover {"
		"  background-color: #138d75;"
		"}"
	);
	productLayout->addWidget(m_saveProductAsBtn);

	topLayout->addLayout(productLayout);

	mainLayout->addWidget(topBar);

	// Content area with left panel and graphics view
	QWidget* contentWidget = new QWidget();
	QHBoxLayout* contentLayout = new QHBoxLayout(contentWidget);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);

	// Left Panel (20%)
	m_controlPanel = new MotionControlPanel();
	m_controlPanel->setMinimumWidth(250);
	m_controlPanel->setMaximumWidth(400);
	contentLayout->addWidget(m_controlPanel, 2);

	// Right Panel (80%)
	m_graphicsView = new MotionGraphicsView();
	contentLayout->addWidget(m_graphicsView, 8);

	mainLayout->addWidget(contentWidget);

	// Connect signals
	connect(m_backButton, &QPushButton::clicked, this, &MotionPage::backToMainMenu);

	connect(m_loadProductBtn, &QPushButton::clicked, this, &MotionPage::onLoadProductClicked);
	connect(m_saveProductBtn, &QPushButton::clicked, this, &MotionPage::onSaveProductClicked);
	connect(m_saveProductAsBtn, &QPushButton::clicked, this, &MotionPage::onSaveProductAsClicked);

	connect(m_controlPanel, &MotionControlPanel::deviceChanged,
		this, &MotionPage::onDeviceChanged);

	connect(m_controlPanel, &MotionControlPanel::moveToPositionClicked,
		this, &MotionPage::onMoveToPositionClicked);

	connect(m_controlPanel, &MotionControlPanel::updatePositionClicked,
		this, &MotionPage::onUpdatePositionClicked);

	connect(m_controlPanel, &MotionControlPanel::createPositionClicked,
		this, &MotionPage::onCreatePositionClicked);

	connect(m_controlPanel, &MotionControlPanel::deletePositionClicked,
		this, &MotionPage::onDeletePositionClicked);

	connect(m_controlPanel, &MotionControlPanel::editWaypointsClicked,
		this, &MotionPage::onEditWaypointsClicked);

	connect(m_controlPanel, &MotionControlPanel::homeClicked,
		this, &MotionPage::onHomeClicked);

	connect(m_controlPanel, &MotionControlPanel::stopClicked,
		this, &MotionPage::onStopClicked);

	connect(m_graphicsView, &MotionGraphicsView::positionSelected,
		this, &MotionPage::onPositionSelected);

	connect(m_graphicsView, &MotionGraphicsView::positionDeselected,
		this, &MotionPage::onPositionDeselected);

	setStyleSheet("QWidget { background-color: #ecf0f1; }");
}

// ============================================================================
// PRODUCT MANAGEMENT
// ============================================================================

void MotionPage::onLoadProductClicked()
{
	// Check if any device is moving
	auto devices = m_motionManager->GetAllDeviceNames();
	for (const auto& device : devices) {
		if (m_motionManager->IsDeviceInMotion(device)) {
			QMessageBox::warning(this, "Cannot Load",
				"Stop all motion before loading new positions");
			return;
		}
	}

	// ✅ CHANGED: Open file dialog - looking for positions.json in product directories
	QString filePath = QFileDialog::getOpenFileName(
		this,
		"Select Product Positions File",
		"./config",  // Start in config directory
		"Position Files (positions.json);;All Files (*.*)"  // ✅ Changed filter from positions.*.json to positions.json
	);

	if (filePath.isEmpty()) {
		return;  // User cancelled
	}

	// Load the product
	if (m_productManager->LoadProduct(filePath.toStdString())) {
		// Export to motion manager
		m_productManager->ExportToMotionManager(*m_motionManager);

		// Update UI
		m_currentProductLabel->setText("Product: " +
			QString::fromStdString(m_productManager->GetCurrentProductName()));
		m_saveProductBtn->setEnabled(true);

		// Save to settings
		QSettings settings;
		settings.setValue("lastProductFile", filePath);

		// Refresh display
		if (!m_currentDevice.isEmpty()) {
			refreshPositionDisplay();
		}

		qDebug() << "Loaded product from:" << filePath;
		QMessageBox::information(this, "Success",
			"Product positions loaded successfully");
	}
	else {
		QMessageBox::critical(this, "Error",
			"Failed to load positions from:\n" + filePath);
	}
}


void MotionPage::onSaveProductClicked()
{
	if (!m_productManager->HasLoadedProduct()) {
		QMessageBox::warning(this, "No Product",
			"No product loaded. Use 'Save As...' to create a new product file.");
		return;
	}

	// Import current positions from motion manager
	m_productManager->ImportFromMotionManager(*m_motionManager);

	// Save to current file
	if (m_productManager->SaveProduct()) {
		QMessageBox::information(this, "Success",
			"Product saved successfully");
	}
	else {
		QMessageBox::critical(this, "Error",
			"Failed to save product file");
	}
}

void MotionPage::onSaveProductAsClicked()
{
	// ✅ Add nullptr check first
	if (!m_productManager) {
		QMessageBox::critical(this, "Error", "Product manager not initialized");
		return;
	}

	// Get product name from user
	bool ok;
	QString productName = QInputDialog::getText(
		this,
		"Save Product As",
		"Enter product name (without .json):",
		QLineEdit::Normal,
		m_productManager->HasLoadedProduct()
		? QString::fromStdString(m_productManager->GetCurrentProductName())
		: "",  // ✅ Empty string if no product loaded
		&ok
	);

	if (!ok || productName.isEmpty()) {
		return;
	}

	// Remove any file extension if user added it
	if (productName.endsWith(".json")) {
		productName = productName.left(productName.length() - 5);
	}

	// Import current positions from motion manager
	m_productManager->ImportFromMotionManager(*m_motionManager);

	// Save as new file
	if (m_productManager->SaveProductAs(productName.toStdString())) {
		m_currentProductLabel->setText("Product: " + productName);
		m_saveProductBtn->setEnabled(true);

		// Save to settings
		QSettings settings;
		settings.setValue("lastProductFile",
			QString::fromStdString(m_productManager->GetCurrentProductPath()));

		QMessageBox::information(this, "Success",
			"Product saved as positions." + productName + ".json");
	}
	else {
		QMessageBox::critical(this, "Error",
			"Failed to save product file");
	}
}
// ============================================================================
// DEVICE AND POSITION MANAGEMENT
// ============================================================================

void MotionPage::loadDevices()
{
	if (!m_motionManager) {
		return;
	}

	std::vector<std::string> deviceNames = m_motionManager->GetAllDeviceNames();

	QStringList devices;
	for (const auto& name : deviceNames) {
		devices << QString::fromStdString(name);
	}

	m_controlPanel->setDevices(devices);

	if (!devices.isEmpty()) {
		m_currentDevice = devices.first();
		refreshPositionDisplay();
	}
}

void MotionPage::loadPositionsForDevice(const QString& deviceName)
{
	m_currentDevice = deviceName;
	m_currentPositions.clear();

	if (!m_motionManager) {
		return;
	}

	// Get positions from motion manager (which has loaded them from product)
	std::vector<std::string> positionNames =
		m_motionManager->GetNamedPositionList(deviceName.toStdString());

	qDebug() << "Loading positions for device:" << deviceName;

	for (const auto& posName : positionNames) {
		PositionStruct pos;
		if (m_motionManager->GetNamedPosition(deviceName.toStdString(),
			posName, pos)) {
			// Transform to global coordinates
			double globalX, globalY, globalZ;
			transformLocalToGlobal(pos.x, pos.y, pos.z, globalX, globalY, globalZ);

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

			qDebug() << "  -" << qPosName
				<< "Local:" << pos.x << pos.y << pos.z
				<< "-> Global:" << globalX << globalY << globalZ;
		}
	}

	// Load label offsets
	loadLabelOffsets();

	// Update graphics view
	m_graphicsView->updatePositions(m_currentPositions, m_labelOffsets);
	m_graphicsView->resetZoom();

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

	globalX = (*matrix)[0] * localX + (*matrix)[1] * localY + (*matrix)[2] * localZ;
	globalY = (*matrix)[3] * localX + (*matrix)[4] * localY + (*matrix)[5] * localZ;
	globalZ = (*matrix)[6] * localX + (*matrix)[7] * localY + (*matrix)[8] * localZ;
}

void MotionPage::onDeviceChanged(const QString& deviceName)
{
	m_currentDevice = deviceName;
	refreshPositionDisplay();
	m_controlPanel->clearSelectedPosition();
	m_selectedPositionName.clear();
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

	// ✅ Use waypoint system instead of direct move
	if (!moveWithWaypoints(m_selectedPositionName)) {
		// Error already shown in moveWithWaypoints
	}
}


// In MotionPage.cpp - Update onUpdatePositionClicked to preserve waypoints

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

	QMessageBox::StandardButton reply = QMessageBox::question(
		this,
		"Update Position",
		QString("Update saved position '%1' with current device position?\n\n"
			"Current: X=%2, Y=%3, Z=%4\n"
			"         U=%5, V=%6, W=%7")
		.arg(m_selectedPositionName)
		.arg(x, 0, 'f', 3)
		.arg(y, 0, 'f', 3)
		.arg(z, 0, 'f', 3)
		.arg(u, 0, 'f', 3)
		.arg(v, 0, 'f', 3)
		.arg(w, 0, 'f', 3),
		QMessageBox::Yes | QMessageBox::No
	);

	if (reply == QMessageBox::Yes) {
		// ✅ CRITICAL - Get existing position to preserve waypoints
		PositionStruct existingPos;
		if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos)) {
			// If position doesn't exist, create new one without waypoints
			existingPos = PositionStruct();
		}

		// ✅ Update coordinates while preserving waypoints
		existingPos.x = x;
		existingPos.y = y;
		existingPos.z = z;
		existingPos.u = u;
		existingPos.v = v;
		existingPos.w = w;
		// waypoints and exit_waypoints are preserved from existingPos

		// Update in motion manager
		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			existingPos);  // ✅ Use PositionStruct overload to preserve waypoints

		QMessageBox::information(this, "Success",
			"Position updated successfully!\n\nRemember to save the product file.");

		// Refresh display
		refreshPositionDisplay();
	}
}


// In MotionPage.cpp - Update onCreatePositionClicked

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

		// Check if position already exists
		if (m_currentPositions.contains(name)) {
			QMessageBox::warning(this, "Position Exists",
				QString("Position '%1' already exists. "
					"Please use a different name.").arg(name));
			return;
		}

		// ✅ Create position with coordinates (no waypoints initially)
		PositionStruct pos;
		pos.x = dialog.getX();
		pos.y = dialog.getY();
		pos.z = dialog.getZ();
		pos.u = dialog.getU();
		pos.v = dialog.getV();
		pos.w = dialog.getW();
		// waypoints and exit_waypoints start empty

		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			name.toStdString(),
			pos);  // ✅ Use PositionStruct overload

		QMessageBox::information(this, "Success",
			QString("Position '%1' created successfully!\n\n"
				"Remember to save the product file.").arg(name));

		// Refresh display
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

	QMessageBox::StandardButton reply = QMessageBox::question(
		this,
		"Delete Position",
		QString("Are you sure you want to delete position '%1'?\n\n"
			"This will automatically save the product.")  // ✅ Updated message
		.arg(m_selectedPositionName),
		QMessageBox::Yes | QMessageBox::No
	);

	if (reply == QMessageBox::Yes) {
		if (m_motionManager->RemoveNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString())) {

			m_selectedPositionName.clear();
			m_controlPanel->clearSelectedPosition();

			// Refresh display
			refreshPositionDisplay();

			// ✅ AUTO-SAVE: Import and save to product file
			if (m_productManager->HasLoadedProduct()) {
				m_productManager->ImportFromMotionManager(*m_motionManager);

				if (m_productManager->SaveProduct()) {
					QMessageBox::information(this, "Success",
						"Position deleted and product saved successfully!");
				}
				else {
					QMessageBox::warning(this, "Partial Success",
						"Position deleted but failed to save product file.\n"
						"Please save manually.");
				}
			}
			else {
				QMessageBox::information(this, "Success",
					"Position deleted successfully!\n\n"
					"No product loaded - remember to save when ready.");
			}
		}
		else {
			QMessageBox::critical(this, "Error", "Failed to delete position.");
		}
	}
}



void MotionPage::onHomeClicked()
{
	if (m_currentDevice.isEmpty()) {
		return;
	}

	QMessageBox::StandardButton reply = QMessageBox::question(
		this,
		"Home Device",
		QString("Send device '%1' to home position?").arg(m_currentDevice),
		QMessageBox::Yes | QMessageBox::No
	);

	if (reply == QMessageBox::Yes) {
		bool success = m_motionManager->MoveToNamedPosition(
			m_currentDevice.toStdString(),
			"home"
		);

		if (!success) {
			QMessageBox::critical(this, "Error", "Failed to home device.");
		}
	}
}

void MotionPage::onStopClicked()
{

	m_motionManager->EmergencyStopAll();
	QMessageBox::information(this, "Stopped", "All devices have been stopped.");

}

// In MotionPage.cpp, update this method:
// In MotionPage.cpp - Update onPositionSelected to pass exit waypoints

void MotionPage::onPositionSelected(const QString& name, double x, double y, double z)
{
	m_selectedPositionName = name;

	if (m_currentPositions.contains(name)) {
		QVector<double> pos = m_currentPositions[name];

		// Get waypoints from motion manager
		PositionStruct fullPos;
		std::vector<std::string> waypoints;
		std::vector<std::string> exitWaypoints;  // ✅ NEW

		if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			name.toStdString(),
			fullPos)) {
			waypoints = fullPos.waypoints;
			exitWaypoints = fullPos.exit_waypoints;  // ✅ NEW
		}

		m_controlPanel->setSelectedPosition(name,
			pos[0], pos[1], pos[2],
			pos[3], pos[4], pos[5],
			waypoints);

		// ✅ UPDATED - Show path preview with both entry and exit waypoints
		m_graphicsView->showPathPreview(name, waypoints, exitWaypoints);
	}
}


void MotionPage::onPositionDeselected()
{
	m_selectedPositionName.clear();
	m_controlPanel->clearSelectedPosition();

	// ✅ ADD THIS - Hide path preview
	m_graphicsView->hidePathPreview();
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
		double globalX, globalY, globalZ;
		transformLocalToGlobal(localX, localY, localZ, globalX, globalY, globalZ);
		m_graphicsView->updateLiveMarker(m_currentDevice, globalX, globalY);
	}
	else {
		m_graphicsView->showLiveMarker(false);
	}
}

// ============================================================================
// LABEL OFFSET MANAGEMENT
// ============================================================================

void MotionPage::loadLabelOffsets()
{
	m_labelOffsets.clear();

	QFile file(m_labelOffsetsFilePath);
	if (!file.open(QIODevice::ReadOnly)) {
		qDebug() << "No label offsets file found, using defaults";
		return;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (!doc.isObject()) {
		qWarning() << "Invalid label offsets JSON format";
		return;
	}

	QJsonObject root = doc.object();

	if (root.contains(m_currentDevice)) {
		QJsonObject deviceObj = root[m_currentDevice].toObject();

		for (auto it = deviceObj.begin(); it != deviceObj.end(); ++it) {
			QString posName = it.key();
			QJsonObject offsetObj = it.value().toObject();

			double offsetX = offsetObj["offsetX"].toDouble();
			double offsetY = offsetObj["offsetY"].toDouble();

			m_labelOffsets[posName] = QPointF(offsetX, offsetY);
		}
	}

	qDebug() << "Loaded" << m_labelOffsets.size() << "label offsets for" << m_currentDevice;
}

void MotionPage::saveLabelOffsets()
{
	QJsonObject root;
	QFile readFile(m_labelOffsetsFilePath);
	if (readFile.open(QIODevice::ReadOnly)) {
		QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
		if (doc.isObject()) {
			root = doc.object();
		}
		readFile.close();
	}

	QJsonObject deviceObj;
	for (auto it = m_labelOffsets.begin(); it != m_labelOffsets.end(); ++it) {
		QJsonObject offsetObj;
		offsetObj["offsetX"] = it.value().x();
		offsetObj["offsetY"] = it.value().y();
		deviceObj[it.key()] = offsetObj;
	}

	root[m_currentDevice] = deviceObj;

	QJsonDocument doc(root);
	QFile writeFile(m_labelOffsetsFilePath);
	if (!writeFile.open(QIODevice::WriteOnly)) {
		qWarning() << "Could not open" << m_labelOffsetsFilePath << "for writing";
		return;
	}

	writeFile.write(doc.toJson(QJsonDocument::Indented));
	writeFile.close();

	qDebug() << "Saved" << m_labelOffsets.size() << "label offsets for" << m_currentDevice;
}

void MotionPage::onLabelOffsetChanged(const QString& name, double offsetX, double offsetY)
{
	m_labelOffsets[name] = QPointF(offsetX, offsetY);
	saveLabelOffsets();
	qDebug() << "Label offset changed:" << name << "(" << offsetX << "," << offsetY << ")";
}

// ============================================================================
// WAYPOINT SYSTEM
// ============================================================================

// In MotionPage.cpp - Update moveWithWaypoints()

bool MotionPage::moveWithWaypoints(const QString& targetPosition) {
	if (!m_motionManager || m_currentDevice.isEmpty()) {
		return false;
	}

	// ✅ NEW - Handle exit waypoints from last position
	if (!m_lastPosition.isEmpty() && m_lastPosition != targetPosition) {
		PositionStruct lastPos;
		if (m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
			m_lastPosition.toStdString(),
			lastPos)) {

			// Execute exit waypoints if they exist
			if (!lastPos.exit_waypoints.empty()) {
				std::cout << "[MotionPage] Executing exit waypoints from "
					<< m_lastPosition.toStdString() << ": ";
				for (const auto& wp : lastPos.exit_waypoints) {
					std::cout << wp << " ";
				}
				std::cout << std::endl;

				for (const auto& waypoint : lastPos.exit_waypoints) {
					std::cout << "[MotionPage] Moving to exit waypoint: " << waypoint << std::endl;

					bool success = m_motionManager->MoveToNamedPosition(
						m_currentDevice.toStdString(),
						waypoint,
						true  // BLOCKING - wait at each exit waypoint
					);

					if (!success) {
						QMessageBox::critical(this, "Exit Waypoint Error",
							QString("Failed to reach exit waypoint: %1\n\nMotion stopped for safety.")
							.arg(QString::fromStdString(waypoint)));
						return false;
					}

					std::cout << "[MotionPage] Reached exit waypoint: " << waypoint << std::endl;
				}
			}
		}
	}

	// Get target position with entry waypoints
	PositionStruct targetPos;
	if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
		targetPosition.toStdString(),
		targetPos)) {
		QMessageBox::critical(this, "Error",
			QString("Position '%1' not found").arg(targetPosition));
		return false;
	}

	// Execute entry waypoints
	if (targetPos.waypoints.empty()) {
		std::cout << "[MotionPage] No entry waypoints - moving directly to "
			<< targetPosition.toStdString() << std::endl;

		bool success = m_motionManager->MoveToNamedPosition(
			m_currentDevice.toStdString(),
			targetPosition.toStdString(),
			false  // Non-blocking
		);

		if (success) {
			m_lastPosition = targetPosition;  // ✅ NEW - Track current position
		}

		return success;
	}

	// Move through entry waypoints (blocking)
	std::cout << "[MotionPage] Moving to " << targetPosition.toStdString()
		<< " via entry waypoints: ";
	for (const auto& wp : targetPos.waypoints) {
		std::cout << wp << " ";
	}
	std::cout << std::endl;

	for (const auto& waypoint : targetPos.waypoints) {
		std::cout << "[MotionPage] Moving to entry waypoint: " << waypoint << std::endl;

		bool success = m_motionManager->MoveToNamedPosition(
			m_currentDevice.toStdString(),
			waypoint,
			true  // BLOCKING - wait at each waypoint
		);

		if (!success) {
			QMessageBox::critical(this, "Entry Waypoint Error",
				QString("Failed to reach entry waypoint: %1\n\nMotion stopped for safety.")
				.arg(QString::fromStdString(waypoint)));
			return false;
		}

		std::cout << "[MotionPage] Reached entry waypoint: " << waypoint << std::endl;
	}

	// Finally move to target (non-blocking)
	std::cout << "[MotionPage] Moving to final target: "
		<< targetPosition.toStdString() << std::endl;

	bool success = m_motionManager->MoveToNamedPosition(
		m_currentDevice.toStdString(),
		targetPosition.toStdString(),
		false
	);

	if (success) {
		m_lastPosition = targetPosition;  // ✅ NEW - Track current position
	}

	return success;
}



// In MotionPage.cpp - Update onEditWaypointsClicked

void MotionPage::onEditWaypointsClicked()
{
	if (m_selectedPositionName.isEmpty()) {
		return;
	}

	// Get current position with waypoints
	PositionStruct currentPos;
	if (!m_motionManager->GetNamedPosition(m_currentDevice.toStdString(),
		m_selectedPositionName.toStdString(),
		currentPos)) {
		QMessageBox::critical(this, "Error", "Failed to get position data");
		return;
	}

	// Get list of all available positions for this device
	std::vector<std::string> availablePositions =
		m_motionManager->GetNamedPositionList(m_currentDevice.toStdString());

	// ✅ Pass both entry and exit waypoints to dialog
	WaypointEditorDialog dialog(m_selectedPositionName,
		currentPos.waypoints,
		currentPos.exit_waypoints,
		availablePositions,
		this);

	if (dialog.exec() == QDialog::Accepted) {
		// ✅ Update both waypoint lists
		currentPos.waypoints = dialog.getWaypoints();
		currentPos.exit_waypoints = dialog.getExitWaypoints();

		// Save updated position
		m_motionManager->AddNamedPosition(m_currentDevice.toStdString(),
			m_selectedPositionName.toStdString(),
			currentPos);

		// Auto-save to product
		if (m_productManager->HasLoadedProduct()) {
			m_productManager->ImportFromMotionManager(*m_motionManager);
			m_productManager->SaveProduct();
		}

		// Refresh display
		refreshPositionDisplay();

		// ✅ UPDATED - Update path preview with both entry and exit waypoints
		m_graphicsView->showPathPreview(m_selectedPositionName,
			currentPos.waypoints,
			currentPos.exit_waypoints);

		QMessageBox::information(this, "Success",
			"Waypoints updated successfully!");
	}
}
