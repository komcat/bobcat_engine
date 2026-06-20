// M4AASerialDataWidget.cpp - see M4AASerialDataWidget.h for the overview.

#include "M4AASerialDataWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QScrollArea>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QCoreApplication>
#include <QFont>
#include <QStringList>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <cmath>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// --- color thresholds (matching view_serial_data.py, brightened for dark UI) -
constexpr double kPoyntThreshold = 0.5;
const QString    kColorOk      = "#4caf50";   // green: |v| < 0.5
const QString    kColorBad     = "#ef5350";   // red:   |v| >= 0.5
const QString    kColorNeutral = "#888888";   // no value
constexpr int    kPollMs       = 1000;

// --- hex-right response rules (docs/M4/hex_right_response_table.md) ----------
// Cell layout (B0/B1 rows x A1.X / A1.Y / A2.X / A2.Y columns):
//   B0: 1=A1.Px 2=A1.Py 3=A2.Px 4=A2.Py
//   B1: 5=A1.Px 6=A1.Py 7=A2.Px 8=A2.Py
const char* kCellNames[9] = {
  "", "A1.B0.Px", "A1.B0.Py", "A2.B0.Px", "A2.B0.Py",
      "A1.B1.Px", "A1.B1.Py", "A2.B1.Px", "A2.B1.Py"
};

struct AxisRule { const char* axis; const char* tag; };  // tag: lg/med/sm

// Per cell: { up-moves, down-moves }. Tags: lg=dominant, med=medium, sm=small.
struct CellRule { std::vector<AxisRule> up; std::vector<AxisRule> down; };

const std::array<CellRule, 9>& cellRules() {
  static const std::array<CellRule, 9> rules = {{
    /* 0 unused */ {},
    /* 1 */ {{{"Y+","lg"},{"U+","lg"},{"W+","med"}}, {{"Y-","lg"},{"U-","lg"},{"W-","med"}}},
    /* 2 */ {{{"X+","lg"},{"Z+","lg"},{"V-","lg"},{"W+","lg"}}, {{"X-","lg"},{"Z-","lg"},{"V+","lg"},{"W-","lg"}}},
    /* 3 */ {{{"Y+","lg"},{"U+","lg"},{"W+","sm"}}, {{"Y-","lg"},{"U-","lg"},{"W-","sm"}}},
    /* 4 */ {{{"X+","lg"},{"Z-","lg"},{"V-","lg"},{"W-","lg"}}, {{"X-","lg"},{"Z+","lg"},{"V+","lg"},{"W+","lg"}}},
    /* 5 */ {{{"Y+","lg"},{"U-","sm"},{"W+","med"}}, {{"Y-","lg"},{"U+","sm"},{"W-","med"}}},
    /* 6 */ {{{"X-","lg"},{"Z+","lg"},{"V-","lg"},{"W-","lg"}}, {{"X+","lg"},{"Z-","lg"},{"V+","lg"},{"W+","lg"}}},
    /* 7 */ {{{"Y+","lg"},{"W+","sm"}}, {{"Y-","lg"},{"W-","sm"}}},
    /* 8 */ {{{"X-","lg"},{"Z+","lg"},{"V+","lg"},{"W+","lg"}}, {{"X+","lg"},{"Z-","lg"},{"V-","lg"},{"W-","lg"}}},
  }};
  return rules;
}

QString tagLabel(const QString& tag) {
  if (tag == "lg")  return "dominant";
  if (tag == "med") return "medium";
  return "small";
}

// section 0=b0/1=b1, side 0=a1/1=a2, axis 0=x/1=y -> cell number 1..8.
int cellNumber(int section, int side, int axis) {
  return section * 4 + side * 2 + axis + 1;
}

QString poyntColor(bool hasValue, double v) {
  if (!hasValue) return kColorNeutral;
  return (std::fabs(v) < kPoyntThreshold) ? kColorOk : kColorBad;
}

QString fmtNumber(bool hasValue, double v) {
  return hasValue ? QString::number(v, 'f', 4) : QString();
}

// Tooltip text for a poynt cell: which hex-right moves drive it toward 0.
QString cellTip(int cellNum, double value) {
  if (cellNum < 1 || cellNum > 8) return QString();
  const CellRule& rule = cellRules()[static_cast<size_t>(cellNum)];
  const bool inSpec = std::fabs(value) < kPoyntThreshold;
  // value >= 0 -> needs to go down; value < 0 -> needs to go up.
  const bool goDown = value >= 0.0;
  const std::vector<AxisRule>& moves = goDown ? rule.down : rule.up;
  const QString arrow = goDown ? QStringLiteral("v") : QStringLiteral("^");

  QStringList grpLg, grpMed, grpSm;
  for (const AxisRule& m : moves) {
    if (QString(m.tag) == "lg")       grpLg  << m.axis;
    else if (QString(m.tag) == "med") grpMed << m.axis;
    else                              grpSm  << m.axis;
  }

  QString status;
  if (inSpec) {
    status = QString("In spec (|v| < %1)").arg(kPoyntThreshold);
  } else {
    status = QString("Out of spec (|v| >= %1); %2")
                 .arg(kPoyntThreshold)
                 .arg(value > 0 ? "too high" : "too low");
  }

  const QString signedVal =
      (value >= 0 ? QString("+") : QString()) + QString::number(value, 'f', 4);

  QStringList lines;
  lines << QString("Cell %1: %2 = %3")
               .arg(QString::number(cellNum), kCellNames[cellNum], signedVal)
        << QString("%1; toward 0 needs %2").arg(status, arrow)
        << QString()
        << "Hex-right moves that drive this cell toward 0:";
  if (!grpLg.isEmpty())  lines << QString("  %1: %2").arg(tagLabel("lg"),  grpLg.join(", "));
  if (!grpMed.isEmpty()) lines << QString("  %1: %2").arg(tagLabel("med"), grpMed.join(", "));
  if (!grpSm.isEmpty())  lines << QString("  %1: %2").arg(tagLabel("sm"),  grpSm.join(", "));
  return lines.join('\n');
}

// QLabel that scales its source pixmap to fit while preserving the aspect
// ratio. Used for the M4AA diagram in the Chart tab. No Q_OBJECT macro, so
// no moc registration needed.
class AspectPixmapLabel : public QLabel {
public:
  using QLabel::QLabel;
  void setSourcePixmap(const QPixmap& pix) {
    m_src = pix;
    rescale();
  }
protected:
  void resizeEvent(QResizeEvent* e) override {
    QLabel::resizeEvent(e);
    rescale();
  }
private:
  void rescale() {
    if (m_src.isNull() || width() <= 4 || height() <= 4) return;
    setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  QPixmap m_src;
};

// Pull a poynt section ("b0"/"b1") out of an entry's "value" object.
m4aa::PoyntSection parseSection(const json& value, const char* name) {
  m4aa::PoyntSection s;
  if (!value.contains(name) || !value[name].is_object()) return s;
  const json& sec = value[name];
  auto readNum = [&](const char* k, bool& has, double& out) {
    if (sec.contains(k) && sec[k].is_number()) {
      has = true;
      out = sec[k].get<double>();
    }
  };
  readNum("poynt_x_deg", s.hasX, s.x);
  readNum("poynt_y_deg", s.hasY, s.y);
  // Beam-detect-server extras (per CLAUDE_step*.lua dut.insertJson schema).
  bool locXHas = false, locYHas = false;
  readNum("loc_x_um", locXHas, s.locX);
  readNum("loc_y_um", locYHas, s.locY);
  s.hasLoc = locXHas && locYHas;
  readNum("z0_um", s.hasZ0, s.z0);
  bool wxHas = false, wyHas = false;
  readNum("waist_x_um", wxHas, s.waistX);
  readNum("waist_y_um", wyHas, s.waistY);
  s.hasWaist = wxHas && wyHas;
  bool m2xHas = false, m2yHas = false;
  readNum("m2_x", m2xHas, s.m2X);
  readNum("m2_y", m2yHas, s.m2Y);
  s.hasM2 = m2xHas && m2yHas;
  return s;
}

// Pull the "tx_rx" sub-object from an entry's "value" object.
m4aa::TxRx parseTxRx(const json& value) {
  m4aa::TxRx t;
  if (!value.contains("tx_rx") || !value["tx_rx"].is_object()) return t;
  const json& tr = value["tx_rx"];
  auto pick = [&](const char* k, double& out) -> bool {
    if (tr.contains(k) && tr[k].is_number()) { out = tr[k].get<double>(); return true; }
    return false;
  };
  const bool a = pick("dx_um", t.dx);
  const bool b = pick("dy_um", t.dy);
  const bool c = pick("dz_um", t.dz);
  const bool d = pick("mag_um", t.mag);
  t.has = a && b && c && d;
  return t;
}

}  // namespace

// =========================================================================
// M4AASerialDataWidget
// =========================================================================

M4AASerialDataWidget::M4AASerialDataWidget(ApplicationContext* context,
                                           QWidget* parent)
    : QWidget(parent), m_context(context) {
  setupUI();

  m_timer = new QTimer(this);
  m_timer->setInterval(kPollMs);
  connect(m_timer, &QTimer::timeout, this, &M4AASerialDataWidget::onTick);
  m_timer->start();

  onTick();  // initial load
}

void M4AASerialDataWidget::setupUI() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 10, 10, 10);

  m_header = new QLabel("No file loaded");
  m_header->setStyleSheet("font-weight: bold;");
  root->addWidget(m_header);

  m_status = new QLabel();
  m_status->setStyleSheet("color: #888;");
  root->addWidget(m_status);

  // --- splitter: entries list (left) | poynt comparison (right) ---
  auto* splitter = new QSplitter(Qt::Horizontal);

  // left: entries list
  auto* leftWrap = new QWidget();
  auto* leftLayout = new QVBoxLayout(leftWrap);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  auto* leftTitle = new QLabel("Entries (newest first)");
  leftTitle->setStyleSheet("font-weight: bold;");
  leftLayout->addWidget(leftTitle);

  m_tree = new QTreeWidget();
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({"Timestamp", "Step", "Loc"});
  m_tree->setRootIsDecorated(false);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setUniformRowHeights(true);
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->resizeSection(0, 180);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_tree->header()->resizeSection(2, 60);
  connect(m_tree, &QTreeWidget::itemSelectionChanged,
          this, &M4AASerialDataWidget::onSelectionChanged);
  leftLayout->addWidget(m_tree);
  splitter->addWidget(leftWrap);

  // right: poynt comparison grid + scatter
  auto* rightWrap = new QWidget();
  auto* rightLayout = new QVBoxLayout(rightWrap);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  auto* rightTitle = new QLabel("Poynt X / Y  (deg)");
  rightTitle->setStyleSheet("font-weight: bold;");
  rightLayout->addWidget(rightTitle);

  m_detailHeader = new QLabel("Select an entry on the left.");
  rightLayout->addWidget(m_detailHeader);

  auto* grid = new QGridLayout();
  const QStringList headers = {"", "A1 Poynt.X", "A1 Poynt.Y",
                               "A2 Poynt.X", "A2 Poynt.Y"};
  for (int c = 0; c < headers.size(); ++c) {
    auto* hh = new QLabel(headers[c]);
    hh->setStyleSheet("font-weight: bold;");
    // Right-align value-column headers so their right edge lines up with the
    // right-aligned number underneath. Column 0 (row label) stays default-left.
    if (c > 0) hh->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(hh, 0, c);
  }
  const std::array<const char*, 2> rowLabels = {"B0 (Rx)", "B1 (Tx)"};
  for (int section = 0; section < 2; ++section) {
    auto* rl = new QLabel(rowLabels[static_cast<size_t>(section)]);
    rl->setStyleSheet("font-weight: bold;");
    grid->addWidget(rl, section + 1, 0);
    for (int side = 0; side < 2; ++side) {
      for (int axis = 0; axis < 2; ++axis) {
        auto* lbl = new QLabel();
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(150);  // fits 7-char values like "-1.1359" at 21pt
        // Bake font-family/size/weight into the stylesheet so subsequent
        // setStyleSheet() calls in updateRight() don't reset them (setting
        // a stylesheet replaces the prior style — setFont() doesn't survive).
        lbl->setStyleSheet(QString(
          "color: %1; font-family: Consolas; font-size: 21pt; font-weight: bold;")
          .arg(kColorNeutral));
        m_valueLabels[section][side][axis] = lbl;
        grid->addWidget(lbl, section + 1, side * 2 + axis + 1);
      }
    }
  }
  rightLayout->addLayout(grid);

  // Bottom area: TX-RX delta panel on the left, reference diagram
  // (assets/M4AA/diagram.png, copied to the build output by the CMake
  // POST_BUILD step) on the right.
  auto* chartRow = new QHBoxLayout();
  chartRow->setContentsMargins(0, 0, 0, 0);
  chartRow->setSpacing(8);

  // TX-RX (dx, dy) panel: one row per location (MOE-A1, MOE-A2), echoing the
  // big poynt grid's right-aligned monospace style.
  auto* txRxBox = new QGroupBox("TX - RX delta  (um)");
  auto* txRxGrid = new QGridLayout(txRxBox);
  txRxGrid->setHorizontalSpacing(18);
  txRxGrid->setVerticalSpacing(8);
  const QStringList txRxHeaders = {"", "dx (um)", "dy (um)"};
  for (int c = 0; c < txRxHeaders.size(); ++c) {
    auto* hh = new QLabel(txRxHeaders[c]);
    hh->setStyleSheet("font-weight: bold;");
    if (c > 0) hh->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    txRxGrid->addWidget(hh, 0, c);
  }
  const std::array<const char*, 2> txRxRowLabels = {"MOE-A1", "MOE-A2"};
  for (int loc = 0; loc < 2; ++loc) {
    auto* rl = new QLabel(txRxRowLabels[static_cast<size_t>(loc)]);
    rl->setStyleSheet("font-weight: bold;");
    txRxGrid->addWidget(rl, loc + 1, 0);
    for (int axis = 0; axis < 2; ++axis) {
      auto* lbl = new QLabel();
      lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      lbl->setMinimumWidth(120);
      lbl->setStyleSheet(QString(
        "color: %1; font-family: Consolas; font-size: 21pt; font-weight: bold;")
        .arg(kColorNeutral));
      m_txRxLabels[loc][axis] = lbl;
      txRxGrid->addWidget(lbl, loc + 1, axis + 1);
    }
  }
  txRxGrid->setColumnStretch(1, 1);
  txRxGrid->setColumnStretch(2, 1);
  txRxGrid->setRowStretch(3, 1);
  chartRow->addWidget(txRxBox, 1);

  auto* diagram = new AspectPixmapLabel();
  diagram->setAlignment(Qt::AlignCenter);
  diagram->setMinimumSize(120, 120);
  const QString diagramPath =
      QCoreApplication::applicationDirPath() + "/assets/M4AA/diagram.png";
  QPixmap pix(diagramPath);
  if (!pix.isNull()) {
    diagram->setSourcePixmap(pix);
  } else {
    diagram->setText(QStringLiteral("(diagram.png missing at %1)").arg(diagramPath));
    diagram->setStyleSheet("color: #888;");
    diagram->setWordWrap(true);
  }
  chartRow->addWidget(diagram, 1);

  rightLayout->addLayout(chartRow, 1);

  // Footer row: "Save final measurement" on the left, "See details..." on the
  // right (opens the non-modal popup).
  auto* footerRow = new QHBoxLayout();
  footerRow->setContentsMargins(0, 4, 0, 0);

  m_saveFinalButton = new QPushButton("Save final measurement");
  m_saveFinalButton->setToolTip(
      "Export the latest MOE-A1 + MOE-A2 measurements (the final pass/fail "
      "state) to data/final_measurement/<serial>_final.json.");
  connect(m_saveFinalButton, &QPushButton::clicked,
          this, &M4AASerialDataWidget::onSaveFinalMeasurement);
  footerRow->addWidget(m_saveFinalButton);

  m_openFolderButton = new QPushButton("Open data folder");
  m_openFolderButton->setToolTip(
      "Open the monitored data folder (applicationDir/data/<product>) in "
      "Windows Explorer.");
  connect(m_openFolderButton, &QPushButton::clicked,
          this, &M4AASerialDataWidget::onOpenDataFolder);
  footerRow->addWidget(m_openFolderButton);

  footerRow->addStretch(1);
  m_detailsButton = new QPushButton("See details...");
  m_detailsButton->setToolTip(
      "Open a separate window with poynt angles, tx_rx delta, and per-beam "
      "loc / waist / M\xc2\xb2 for MOE-A1 and MOE-A2.");
  connect(m_detailsButton, &QPushButton::clicked,
          this, &M4AASerialDataWidget::openDetailsDialog);
  footerRow->addWidget(m_detailsButton);
  rightLayout->addLayout(footerRow);

  splitter->addWidget(rightWrap);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3);
  root->addWidget(splitter, 1);
}

// Lazily builds the non-modal details popup on first open. The dialog owns
// two location sections (MOE-A1, MOE-A2); each shows poynt X/Y for B0 and B1,
// the tx_rx delta, and per-beam loc / waist / M².
void M4AASerialDataWidget::ensureDetailsDialog() {
  if (m_detailsDialog) return;

  m_detailsDialog = new QDialog(this, Qt::Window);
  m_detailsDialog->setAttribute(Qt::WA_DeleteOnClose, false);
  m_detailsDialog->setWindowTitle("M4AA - beam details");
  m_detailsDialog->resize(720, 560);

  auto* dlgRoot = new QVBoxLayout(m_detailsDialog);
  dlgRoot->setContentsMargins(10, 10, 10, 10);

  auto* scroll = new QScrollArea(m_detailsDialog);
  scroll->setWidgetResizable(true);
  auto* inner = new QWidget();
  scroll->setWidget(inner);
  dlgRoot->addWidget(scroll, 1);

  auto* outer = new QVBoxLayout(inner);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(12);

  const auto valueStyle = QStringLiteral(
      "color: #e3f2fd; font-family: Consolas; font-weight: bold;");
  const auto labelStyle = QStringLiteral("color: #cfd8dc;");
  const auto unitStyle  = QStringLiteral("color: #90a4ae; font-family: Consolas;");
  const auto sectionHeaderStyle = QStringLiteral(
      "color: #64b5f6; font-weight: bold; padding-top: 4px;");
  const auto subHeaderStyle = QStringLiteral(
      "color: #64b5f6; font-weight: bold; padding-top: 4px;");

  auto buildLocationSection = [&](const QString& locName,
                                  LocationDetailLabels& out) {
    auto* dl = new QGridLayout();
    dl->setHorizontalSpacing(14);
    dl->setVerticalSpacing(4);
    // Columns: 0=label0, 1=value0, 2=unit0, 3=gap, 4=label1, 5=value1, 6=unit1.
    dl->setColumnStretch(1, 1);
    dl->setColumnStretch(5, 1);
    dl->setColumnMinimumWidth(3, 24);

    auto makeCell = [&](int row, int colBase, const QString& label,
                        const QString& unit, QLabel*& valueOut) {
      auto* l = new QLabel(label);
      l->setStyleSheet(labelStyle);
      auto* v = new QLabel("-");
      v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      v->setStyleSheet(valueStyle);
      auto* u = new QLabel(unit);
      u->setStyleSheet(unitStyle);
      dl->addWidget(l, row, colBase + 0);
      dl->addWidget(v, row, colBase + 1);
      dl->addWidget(u, row, colBase + 2);
      valueOut = v;
    };
    auto makeSubHeader = [&](int row, int colBase, int colSpan,
                             const QString& text) {
      auto* h = new QLabel(text);
      h->setStyleSheet(subHeaderStyle);
      dl->addWidget(h, row, colBase, 1, colSpan);
    };

    int r = 0;
    out.header = new QLabel(locName);
    out.header->setStyleSheet(sectionHeaderStyle);
    dl->addWidget(out.header, r, 0, 1, 7);
    ++r;

    // Poynt angles: B0 row then B1 row, X on the left, Y on the right.
    makeSubHeader(r, 0, 7, "  Poynt X / Y  (deg)");
    ++r;
    makeCell(r, 0, "  B0 (Rx) X", "deg", out.b0.poyntX);
    makeCell(r, 4, "  B0 (Rx) Y", "deg", out.b0.poyntY);
    ++r;
    makeCell(r, 0, "  B1 (Tx) X", "deg", out.b1.poyntX);
    makeCell(r, 4, "  B1 (Tx) Y", "deg", out.b1.poyntY);
    ++r;

    makeSubHeader(r, 0, 7, "  TX-RX delta (B0 - B1)");
    ++r;
    makeCell(r, 0, "  dx_um", "um", out.dx);
    makeCell(r, 4, "  dy_um", "um", out.dy);
    ++r;

    makeSubHeader(r, 0, 3, "  B0 (Rx)");
    makeSubHeader(r, 4, 3, "  B1 (Tx)");
    ++r;

    struct FieldRow { const char* label; const char* unit;
                      QLabel** b0; QLabel** b1; };
    const FieldRow rows[] = {
      {"  loc X",   "um", &out.b0.locX,   &out.b1.locX  },
      {"  loc Y",   "um", &out.b0.locY,   &out.b1.locY  },
      {"  z0",      "um", &out.b0.z0,     &out.b1.z0    },
      {"  waist X", "um", &out.b0.waistX, &out.b1.waistX},
      {"  waist Y", "um", &out.b0.waistY, &out.b1.waistY},
      {"  M\xc2\xb2 X", "", &out.b0.m2X,  &out.b1.m2X   },
      {"  M\xc2\xb2 Y", "", &out.b0.m2Y,  &out.b1.m2Y   },
    };
    for (const FieldRow& fr : rows) {
      makeCell(r, 0, fr.label, fr.unit, *fr.b0);
      makeCell(r, 4, fr.label, fr.unit, *fr.b1);
      ++r;
    }
    outer->addLayout(dl);
  };

  buildLocationSection("MOE-A1", m_details.a1);
  buildLocationSection("MOE-A2", m_details.a2);
  outer->addStretch(1);
}

void M4AASerialDataWidget::openDetailsDialog() {
  ensureDetailsDialog();
  // Push the current selection's data into the freshly built (or already
  // visible) dialog so it doesn't show stale "-" placeholders.
  if (QTreeWidgetItem* item = m_tree->currentItem()) {
    const int selIdx = item->data(0, Qt::UserRole).toInt();
    refreshDetailsDialog(selIdx,
                        findLatestByLocation(selIdx, "MOE-A1"),
                        findLatestByLocation(selIdx, "MOE-A2"));
  } else {
    clearDetailsDialog();
  }
  m_detailsDialog->show();
  m_detailsDialog->raise();
  m_detailsDialog->activateWindow();
}

void M4AASerialDataWidget::clearDetailsDialog() {
  if (!m_detailsDialog) return;
  LocationDetailLabels* locs[2] = {&m_details.a1, &m_details.a2};
  const char* locNames[2] = {"MOE-A1", "MOE-A2"};
  for (int i = 0; i < 2; ++i) {
    LocationDetailLabels& d = *locs[i];
    if (d.header) d.header->setText(locNames[i]);
    QLabel* fields[] = {
      d.dx, d.dy,
      d.b0.poyntX, d.b0.poyntY,
      d.b0.locX, d.b0.locY, d.b0.z0, d.b0.waistX, d.b0.waistY, d.b0.m2X, d.b0.m2Y,
      d.b1.poyntX, d.b1.poyntY,
      d.b1.locX, d.b1.locY, d.b1.z0, d.b1.waistX, d.b1.waistY, d.b1.m2X, d.b1.m2Y,
    };
    for (QLabel* l : fields) if (l) l->setText("-");
  }
}

void M4AASerialDataWidget::refreshDetailsDialog(int selIdx, int a1Idx, int a2Idx) {
  // No-op if the dialog has never been opened — labels don't exist yet.
  if (!m_detailsDialog) return;

  auto setNum = [](QLabel* lbl, bool has, double v, int prec) {
    if (!lbl) return;
    lbl->setText(has ? QString::number(v, 'f', prec) : QStringLiteral("-"));
  };

  const int locIdx[2] = {a1Idx, a2Idx};
  const char* locNames[2] = {"MOE-A1", "MOE-A2"};
  LocationDetailLabels* locRefs[2] = {&m_details.a1, &m_details.a2};
  for (int i = 0; i < 2; ++i) {
    LocationDetailLabels& d = *locRefs[i];
    if (locIdx[i] < 0) {
      if (d.header) d.header->setText(QString("%1  -  (no entry)").arg(locNames[i]));
      QLabel* fields[] = {
        d.dx, d.dy,
        d.b0.poyntX, d.b0.poyntY,
        d.b0.locX, d.b0.locY, d.b0.z0, d.b0.waistX, d.b0.waistY, d.b0.m2X, d.b0.m2Y,
        d.b1.poyntX, d.b1.poyntY,
        d.b1.locX, d.b1.locY, d.b1.z0, d.b1.waistX, d.b1.waistY, d.b1.m2X, d.b1.m2Y,
      };
      for (QLabel* l : fields) if (l) l->setText("-");
      continue;
    }
    const m4aa::SerialEntry& e = m_entries[static_cast<size_t>(locIdx[i])];
    if (d.header) {
      const bool isPrev = (locIdx[i] != selIdx);
      d.header->setText(QString("%1  -  %2%3")
                           .arg(locNames[i], e.timestamp,
                                isPrev ? "  (prev)" : ""));
    }
    setNum(d.dx, e.txRx.has, e.txRx.dx, 3);
    setNum(d.dy, e.txRx.has, e.txRx.dy, 3);
    const m4aa::PoyntSection* beams[2] = {&e.b0, &e.b1};
    BeamDetailLabels* brefs[2] = {&d.b0, &d.b1};
    for (int b = 0; b < 2; ++b) {
      const m4aa::PoyntSection& s = *beams[b];
      BeamDetailLabels& r = *brefs[b];
      setNum(r.poyntX, s.hasX,     s.x,      4);
      setNum(r.poyntY, s.hasY,     s.y,      4);
      setNum(r.locX,   s.hasLoc,   s.locX,   3);
      setNum(r.locY,   s.hasLoc,   s.locY,   3);
      setNum(r.z0,     s.hasZ0,    s.z0,     3);
      setNum(r.waistX, s.hasWaist, s.waistX, 2);
      setNum(r.waistY, s.hasWaist, s.waistY, 2);
      setNum(r.m2X,    s.hasM2,    s.m2X,    3);
      setNum(r.m2Y,    s.hasM2,    s.m2Y,    3);
    }
  }
}

QString M4AASerialDataWidget::watchDir() const {
  if (!m_context || !m_context->productPositionManager) return QString();
  if (!m_context->productPositionManager->HasLoadedProduct()) return QString();
  const QString product = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductName());
  if (product.isEmpty()) return QString();
  return QCoreApplication::applicationDirPath() + "/data/" + product;
}

// Routine worker tick: find the most recently modified *.json in the watch
// folder and reload if a newer file appeared or the current file was rewritten.
void M4AASerialDataWidget::onTick() {
  const QString dir = watchDir();

  // Detect a product switch and drop stale entries.
  if (dir != m_lastWatchDir) {
    m_lastWatchDir = dir;
    m_currentPath.clear();
    m_currentMtime = -1;
    m_entries.clear();
    m_serialNumber.clear();
    m_productName.clear();
    m_createdTimestamp.clear();
    m_tree->clear();
    clearRight();
  }

  if (dir.isEmpty()) {
    m_status->setText("● idle  -  no product loaded");
    m_header->setText("No file loaded");
    return;
  }

  fs::path latest;
  long long latestMtime = -1;
  std::error_code ec;
  fs::path base(dir.toStdString());
  if (fs::is_directory(base, ec)) {
    for (auto it = fs::recursive_directory_iterator(
             base, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      if (it->path().extension() != ".json") continue;
      auto t = fs::last_write_time(it->path(), ec);
      if (ec) { ec.clear(); continue; }
      long long m = static_cast<long long>(t.time_since_epoch().count());
      if (m > latestMtime) {
        latestMtime = m;
        latest = it->path();
      }
    }
  }

  m_status->setText(QString("● monitoring  -  %1  (every %2 ms)")
                        .arg(dir).arg(kPollMs));

  if (latestMtime < 0) {
    if (m_currentPath.isEmpty()) {
      m_header->setText(QString("No JSON found in %1 - waiting...").arg(dir));
    }
    return;
  }

  const QString latestPath = QString::fromStdString(latest.string());
  if (latestPath != m_currentPath || latestMtime != m_currentMtime) {
    m_currentMtime = latestMtime;
    reload(latestPath, /*jumpToNewest=*/true);
  }
}

void M4AASerialDataWidget::reload(const QString& path, bool jumpToNewest) {
  std::ifstream in(path.toStdString());
  if (!in.is_open()) return;

  json data;
  {
    json parsed = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) return;  // file may be mid-write
    data = std::move(parsed);
  }

  // Remember the current selection so it can be restored after repopulating.
  QString prevKey;
  if (!jumpToNewest && m_tree->currentItem()) {
    int idx = m_tree->currentItem()->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(m_entries.size())) {
      const auto& e = m_entries[static_cast<size_t>(idx)];
      prevKey = e.timestamp + "|" + e.key + "|" + e.location;
    }
  }

  m_entries.clear();
  if (data.contains("entries") && data["entries"].is_array()) {
    for (const json& e : data["entries"]) {
      m4aa::SerialEntry entry;
      if (e.contains("timestamp") && e["timestamp"].is_string())
        entry.timestamp = QString::fromStdString(e["timestamp"].get<std::string>());
      if (e.contains("key") && e["key"].is_string())
        entry.key = QString::fromStdString(e["key"].get<std::string>());
      if (e.contains("value") && e["value"].is_object()) {
        const json& v = e["value"];
        if (v.contains("location") && v["location"].is_string())
          entry.location = QString::fromStdString(v["location"].get<std::string>());
        entry.b0 = parseSection(v, "b0");
        entry.b1 = parseSection(v, "b1");
        entry.txRx = parseTxRx(v);
      }
      m_entries.push_back(entry);
    }
  }

  m_currentPath = path;

  auto jsonStr = [&](const char* k) -> QString {
    return (data.contains(k) && data[k].is_string())
               ? QString::fromStdString(data[k].get<std::string>())
               : QString("?");
  };
  m_serialNumber     = jsonStr("serial_number");
  m_productName      = jsonStr("product_name");
  m_createdTimestamp = jsonStr("creation_timestamp");
  m_header->setText(QString("Serial: %1   |   Product: %2   |   Created: %3   |   File: %4")
                        .arg(m_serialNumber,
                             m_productName,
                             m_createdTimestamp,
                             QString::fromStdString(
                                 fs::path(path.toStdString()).filename().string())));

  populateList();

  // Restore selection (or fall back to the newest entry).
  QTreeWidgetItem* target = nullptr;
  if (!prevKey.isEmpty()) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
      QTreeWidgetItem* it = m_tree->topLevelItem(i);
      int idx = it->data(0, Qt::UserRole).toInt();
      const auto& e = m_entries[static_cast<size_t>(idx)];
      if (prevKey == e.timestamp + "|" + e.key + "|" + e.location) {
        target = it;
        break;
      }
    }
  }
  if (!target && m_tree->topLevelItemCount() > 0) {
    target = m_tree->topLevelItem(0);  // newest-first -> latest entry
  }
  if (target) {
    m_tree->setCurrentItem(target);
    // Refresh explicitly: setCurrentItem may not re-emit if the same row
    // index is reused after a file rewrite.
    updateRight(target->data(0, Qt::UserRole).toInt());
  } else {
    clearRight();
  }
}

void M4AASerialDataWidget::populateList() {
  m_tree->clear();
  // Display newest first; UserRole holds the original chronological index.
  for (int idx = static_cast<int>(m_entries.size()) - 1; idx >= 0; --idx) {
    const auto& e = m_entries[static_cast<size_t>(idx)];
    QString locShort = e.location;
    locShort.replace("MOE-", "");
    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, e.timestamp);
    item->setText(1, e.key);
    item->setText(2, locShort);
    item->setTextAlignment(2, Qt::AlignCenter);
    item->setData(0, Qt::UserRole, idx);
  }
}

int M4AASerialDataWidget::findLatestByLocation(int uptoIdx,
                                               const QString& loc) const {
  for (int i = uptoIdx; i >= 0; --i) {
    if (m_entries[static_cast<size_t>(i)].location == loc) return i;
  }
  return -1;
}

void M4AASerialDataWidget::onSelectionChanged() {
  QTreeWidgetItem* item = m_tree->currentItem();
  if (!item) { clearRight(); return; }
  updateRight(item->data(0, Qt::UserRole).toInt());
}

void M4AASerialDataWidget::onOpenDataFolder() {
  const QString dir = watchDir();
  if (dir.isEmpty()) {
    QMessageBox::warning(this, "Open data folder",
                         "No product loaded - data folder is unknown.");
    return;
  }
  QDir().mkpath(dir);  // ensure it exists so Explorer has something to open
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void M4AASerialDataWidget::onSaveFinalMeasurement() {
  if (m_entries.empty() || m_serialNumber.isEmpty() || m_serialNumber == "?") {
    QMessageBox::warning(this, "Save final measurement",
                         "No measurement data loaded yet.");
    return;
  }

  // "Final" = the most recent entry for each location across the whole file.
  const int last = static_cast<int>(m_entries.size()) - 1;
  const int a1Idx = findLatestByLocation(last, "MOE-A1");
  const int a2Idx = findLatestByLocation(last, "MOE-A2");
  if (a1Idx < 0 && a2Idx < 0) {
    QMessageBox::warning(this, "Save final measurement",
                         "No MOE-A1 or MOE-A2 entries found to export.");
    return;
  }

  // Re-emit a parsed poynt section as JSON, omitting fields that were absent.
  auto sectionJson = [](const m4aa::PoyntSection& s) {
    json o = json::object();
    if (s.hasX)     o["poynt_x_deg"] = s.x;
    if (s.hasY)     o["poynt_y_deg"] = s.y;
    if (s.hasLoc)   { o["loc_x_um"] = s.locX; o["loc_y_um"] = s.locY; }
    if (s.hasZ0)    o["z0_um"] = s.z0;
    if (s.hasWaist) { o["waist_x_um"] = s.waistX; o["waist_y_um"] = s.waistY; }
    if (s.hasM2)    { o["m2_x"] = s.m2X; o["m2_y"] = s.m2Y; }
    return o;
  };
  auto entryJson = [&](int idx) {
    const m4aa::SerialEntry& e = m_entries[static_cast<size_t>(idx)];
    json o = json::object();
    o["timestamp"] = e.timestamp.toStdString();
    o["key"]       = e.key.toStdString();
    o["location"]  = e.location.toStdString();
    o["b0"]        = sectionJson(e.b0);
    o["b1"]        = sectionJson(e.b1);
    if (e.txRx.has) {
      o["tx_rx"] = {{"dx_um", e.txRx.dx}, {"dy_um", e.txRx.dy},
                    {"dz_um", e.txRx.dz}, {"mag_um", e.txRx.mag}};
    }
    return o;
  };

  json out;
  out["serial_number"]      = m_serialNumber.toStdString();
  out["product_name"]       = m_productName.toStdString();
  out["creation_timestamp"] = m_createdTimestamp.toStdString();
  out["source_file"] =
      fs::path(m_currentPath.toStdString()).filename().string();
  out["exported_at"] = QDateTime::currentDateTimeUtc()
                           .toString(Qt::ISODateWithMs)
                           .toStdString();
  json fm = json::object();
  if (a1Idx >= 0) fm["a1"] = entryJson(a1Idx);
  if (a2Idx >= 0) fm["a2"] = entryJson(a2Idx);
  out["final_measurement"] = fm;

  // Write to <applicationDir>/data/final_measurement/<serial>_final.json.
  std::error_code ec;
  fs::path dir = fs::path(QCoreApplication::applicationDirPath().toStdString()) /
                 "data" / "final_measurement";
  fs::create_directories(dir, ec);
  if (ec) {
    QMessageBox::critical(this, "Save final measurement",
                          QString("Could not create folder:\n%1\n\n%2")
                              .arg(QString::fromStdString(dir.string()),
                                   QString::fromStdString(ec.message())));
    return;
  }
  const fs::path file = dir / (m_serialNumber.toStdString() + "_final.json");
  std::ofstream o(file, std::ios::binary | std::ios::trunc);
  if (!o.is_open()) {
    QMessageBox::critical(this, "Save final measurement",
                          QString("Could not open file for writing:\n%1")
                              .arg(QString::fromStdString(file.string())));
    return;
  }
  o << out.dump(2);
  o.close();

  const QString savedPath = QString::fromStdString(file.string());
  m_status->setText(QString("● saved final measurement -> %1").arg(savedPath));
  QMessageBox::information(this, "Save final measurement",
                           QString("Saved final measurement to:\n%1").arg(savedPath));
}

void M4AASerialDataWidget::clearRight() {
  m_detailHeader->setText("Select an entry on the left.");
  for (auto& byside : m_valueLabels)
    for (auto& byaxis : byside)
      for (QLabel* lbl : byaxis) {
        lbl->setText(QString());
        lbl->setStyleSheet(QString("color: %1;").arg(kColorNeutral));
        lbl->setToolTip(QString());
      }
  for (auto& byloc : m_txRxLabels)
    for (QLabel* lbl : byloc) {
      lbl->setText(QString());
      lbl->setStyleSheet(QString(
        "color: %1; font-family: Consolas; font-size: 21pt; font-weight: bold;")
        .arg(kColorNeutral));
    }
  clearDetailsDialog();
}

void M4AASerialDataWidget::updateRight(int selIdx) {
  if (selIdx < 0 || selIdx >= static_cast<int>(m_entries.size())) {
    clearRight();
    return;
  }

  // A1/A2 fall back to the most recent previous entry of that location.
  const int a1Idx = findLatestByLocation(selIdx, "MOE-A1");
  const int a2Idx = findLatestByLocation(selIdx, "MOE-A2");

  const auto& sel = m_entries[static_cast<size_t>(selIdx)];
  auto tsLabel = [&](int idx) -> QString {
    if (idx < 0) return "-";
    QString ts = m_entries[static_cast<size_t>(idx)].timestamp;
    return (idx != selIdx) ? ts + "  (prev)" : ts;
  };
  m_detailHeader->setText(
      QString("Selected: %1   %2\nA1: %3   |   A2: %4")
          .arg(sel.timestamp, sel.key, tsLabel(a1Idx), tsLabel(a2Idx)));

  const m4aa::SerialEntry* sides[2] = {
      a1Idx >= 0 ? &m_entries[static_cast<size_t>(a1Idx)] : nullptr,
      a2Idx >= 0 ? &m_entries[static_cast<size_t>(a2Idx)] : nullptr};

  // Fill the 8-cell grid.
  for (int section = 0; section < 2; ++section) {
    for (int side = 0; side < 2; ++side) {
      const m4aa::PoyntSection* sec = nullptr;
      if (sides[side]) {
        sec = (section == 0) ? &sides[side]->b0 : &sides[side]->b1;
      }
      for (int axis = 0; axis < 2; ++axis) {
        const bool has = sec && (axis == 0 ? sec->hasX : sec->hasY);
        const double v = !sec ? 0.0 : (axis == 0 ? sec->x : sec->y);
        QLabel* lbl = m_valueLabels[section][side][axis];
        lbl->setText(fmtNumber(has, v));
        // Re-apply the full stylesheet (font + color) — setStyleSheet replaces
        // the prior style, so we have to include the font properties here too.
        lbl->setStyleSheet(QString(
          "color: %1; font-family: Consolas; font-size: 21pt; font-weight: bold;")
          .arg(poyntColor(has, v)));
        lbl->setToolTip(has ? cellTip(cellNumber(section, side, axis), v)
                            : QString());
      }
    }
  }

  // Fill the TX-RX delta panel (dx, dy per location).
  for (int loc = 0; loc < 2; ++loc) {
    const bool has = sides[loc] && sides[loc]->txRx.has;
    const double vals[2] = {
        has ? sides[loc]->txRx.dx : 0.0,
        has ? sides[loc]->txRx.dy : 0.0};
    for (int axis = 0; axis < 2; ++axis) {
      QLabel* lbl = m_txRxLabels[loc][axis];
      lbl->setText(has ? QString::number(vals[axis], 'f', 3) : QString());
      lbl->setStyleSheet(QString(
        "color: %1; font-family: Consolas; font-size: 21pt; font-weight: bold;")
        .arg(has ? kColorOk : kColorNeutral));
    }
  }

  // Push the same A1/A2 entries into the popup (no-op if it hasn't been opened).
  refreshDetailsDialog(selIdx, a1Idx, a2Idx);
}
