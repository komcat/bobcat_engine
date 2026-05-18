// M4AASerialDataWidget.cpp - see M4AASerialDataWidget.h for the overview.

#include "M4AASerialDataWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QCoreApplication>
#include <QFont>
#include <QStringList>
#include <QAbstractItemView>

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

// Pull a poynt section ("b0"/"b1") out of an entry's "value" object.
m4aa::PoyntSection parseSection(const json& value, const char* name) {
  m4aa::PoyntSection s;
  if (!value.contains(name) || !value[name].is_object()) return s;
  const json& sec = value[name];
  if (sec.contains("poynt_x_deg") && sec["poynt_x_deg"].is_number()) {
    s.hasX = true;
    s.x = sec["poynt_x_deg"].get<double>();
  }
  if (sec.contains("poynt_y_deg") && sec["poynt_y_deg"].is_number()) {
    s.hasY = true;
    s.y = sec["poynt_y_deg"].get<double>();
  }
  return s;
}

}  // namespace

// =========================================================================
// PoyntScatterWidget
// =========================================================================
namespace m4aa {

namespace {
constexpr double kAxisMin = -5.0;
constexpr double kAxisMax = 5.0;
constexpr double kAcceptR = 0.5;
}  // namespace

PoyntScatterWidget::PoyntScatterWidget(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(220);
}

void PoyntScatterWidget::setPoints(const std::vector<Point>& pts) {
  m_points = pts;
  update();
}

void PoyntScatterWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int w = width();
  const int h = height();
  p.fillRect(rect(), QColor("#fafafa"));
  if (w <= 40 || h <= 40) return;

  const int marginL = 44, marginR = 14, marginT = 14, marginB = 30;
  const int plot = std::min(w - marginL - marginR, h - marginT - marginB);
  if (plot <= 0) return;
  const int ox = marginL;
  const int oy = marginT;

  const double range = kAxisMax - kAxisMin;
  auto toPx = [&](double x, double y) -> QPointF {
    return QPointF(ox + (x - kAxisMin) / range * plot,
                   oy + (kAxisMax - y) / range * plot);
  };

  // plot frame
  p.setPen(QColor("#bbbbbb"));
  p.drawRect(ox, oy, plot, plot);

  // minor grid every 1 unit
  p.setPen(QColor("#ececec"));
  for (int v = static_cast<int>(kAxisMin) + 1; v < static_cast<int>(kAxisMax); ++v) {
    if (v == 0) continue;
    QPointF gx = toPx(v, 0);
    p.drawLine(QPointF(gx.x(), oy), QPointF(gx.x(), oy + plot));
    QPointF gy = toPx(0, v);
    p.drawLine(QPointF(ox, gy.y()), QPointF(ox + plot, gy.y()));
  }

  // axes
  QPointF origin = toPx(0, 0);
  p.setPen(QColor("#888888"));
  p.drawLine(QPointF(origin.x(), oy), QPointF(origin.x(), oy + plot));
  p.drawLine(QPointF(ox, origin.y()), QPointF(ox + plot, origin.y()));

  // acceptance circle r=0.5
  const double rPx = kAcceptR / range * plot;
  QPen circPen{QColor(kColorOk)};
  circPen.setWidth(2);
  circPen.setStyle(Qt::DashLine);
  p.setPen(circPen);
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(origin, rPx, rPx);
  p.setPen(QColor(kColorOk));
  p.drawText(QPointF(origin.x() + rPx + 4, origin.y() - rPx - 4),
             QString("r=%1").arg(kAcceptR));

  // tick labels
  p.setPen(QColor("#555555"));
  for (int v : {static_cast<int>(kAxisMin), 0, static_cast<int>(kAxisMax)}) {
    QPointF tx = toPx(v, 0);
    p.drawText(QRectF(tx.x() - 15, oy + plot + 2, 30, 14),
               Qt::AlignCenter, QString::number(v));
    QPointF ty = toPx(0, v);
    p.drawText(QRectF(ox - 36, ty.y() - 7, 30, 14),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(v));
  }

  // axis titles
  QFont titleFont = p.font();
  titleFont.setBold(true);
  p.setFont(titleFont);
  p.setPen(QColor("#333333"));
  p.drawText(QRectF(ox, oy + plot + 14, plot, 14),
             Qt::AlignCenter, "Poynt.X (deg)");
  p.save();
  p.translate(ox - 30, oy + plot / 2.0);
  p.rotate(-90);
  p.drawText(QRectF(-plot / 2.0, -8, plot, 16),
             Qt::AlignCenter, "Poynt.Y (deg)");
  p.restore();

  // points (skip any outside the visible range)
  p.setFont(QFont());
  for (const Point& pt : m_points) {
    if (pt.x < kAxisMin || pt.x > kAxisMax ||
        pt.y < kAxisMin || pt.y > kAxisMax) {
      continue;
    }
    QPointF c = toPx(pt.x, pt.y);
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(pt.color);
    p.drawEllipse(c, 5, 5);
  }
}

}  // namespace m4aa

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
    grid->addWidget(hh, 0, c);
  }
  const std::array<const char*, 2> rowLabels = {"B0 (Rx)", "B1 (Tx)"};
  QFont valueFont("Consolas", 14, QFont::Bold);
  for (int section = 0; section < 2; ++section) {
    auto* rl = new QLabel(rowLabels[static_cast<size_t>(section)]);
    rl->setStyleSheet("font-weight: bold;");
    grid->addWidget(rl, section + 1, 0);
    for (int side = 0; side < 2; ++side) {
      for (int axis = 0; axis < 2; ++axis) {
        auto* lbl = new QLabel();
        lbl->setFont(valueFont);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(110);
        lbl->setStyleSheet(QString("color: %1;").arg(kColorNeutral));
        m_valueLabels[section][side][axis] = lbl;
        grid->addWidget(lbl, section + 1, side * 2 + axis + 1);
      }
    }
  }
  rightLayout->addLayout(grid);

  m_scatter = new m4aa::PoyntScatterWidget();
  rightLayout->addWidget(m_scatter, 1);

  splitter->addWidget(rightWrap);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3);
  root->addWidget(splitter, 1);
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
  m_header->setText(QString("Serial: %1   |   Product: %2   |   Created: %3   |   File: %4")
                        .arg(jsonStr("serial_number"),
                             jsonStr("product_name"),
                             jsonStr("creation_timestamp"),
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

void M4AASerialDataWidget::clearRight() {
  m_detailHeader->setText("Select an entry on the left.");
  for (auto& byside : m_valueLabels)
    for (auto& byaxis : byside)
      for (QLabel* lbl : byaxis) {
        lbl->setText(QString());
        lbl->setStyleSheet(QString("color: %1;").arg(kColorNeutral));
        lbl->setToolTip(QString());
      }
  m_scatter->setPoints({});
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
        lbl->setStyleSheet(QString("color: %1;").arg(poyntColor(has, v)));
        lbl->setToolTip(has ? cellTip(cellNumber(section, side, axis), v)
                            : QString());
      }
    }
  }

  // Build scatter points: A1/A2 x B0/B1.
  static const struct {
    const char* label; int side; int section; const char* color;
  } series[4] = {
    {"A1 B0 (Rx)", 0, 0, "#d32f2f"},
    {"A1 B1 (Tx)", 0, 1, "#f57c00"},
    {"A2 B0 (Rx)", 1, 0, "#1976d2"},
    {"A2 B1 (Tx)", 1, 1, "#388e3c"},
  };
  std::vector<m4aa::PoyntScatterWidget::Point> pts;
  for (const auto& s : series) {
    if (!sides[s.side]) continue;
    const m4aa::PoyntSection& sec =
        (s.section == 0) ? sides[s.side]->b0 : sides[s.side]->b1;
    if (sec.hasX && sec.hasY) {
      pts.push_back({QString(s.label), sec.x, sec.y, QColor(s.color)});
    }
  }
  m_scatter->setPoints(pts);
}
