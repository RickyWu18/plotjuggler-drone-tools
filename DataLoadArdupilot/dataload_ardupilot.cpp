#include "dataload_ardupilot.h"
#include "ardupilot_parser.h"
#include "ardupilot_info_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QVBoxLayout>
#include <stdexcept>

const std::vector<const char*>& DataLoadArdupilot::compatibleFileExtensions() const
{
  static std::vector<const char*> exts = { "bin", "BIN" };
  return exts;
}

bool DataLoadArdupilot::readDataFromFile(PJ::FileLoadInfo* info,
                                         PJ::PlotDataMapRef& dest)
{
  QSettings pref("DataLoadArdupilot", "settings");
  bool show_units = pref.value("show_units", false).toBool();

  {
    QDialog dlg;
    dlg.setWindowTitle("ArduPilot Load Settings");
    auto* layout  = new QVBoxLayout(&dlg);
    auto* cb      = new QCheckBox("Append units to series names (e.g. Roll \xe2\x86\x92 Roll(deg))", &dlg);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    cb->setChecked(show_units);
    layout->addWidget(cb);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    show_units = cb->isChecked();
    pref.setValue("show_units", show_units);
  }

  QFile file(info->filename);
  if (!file.open(QIODevice::ReadOnly))
  {
    throw std::runtime_error("ArduPilot: failed to open file: " +
                             info->filename.toStdString());
  }

  const qint64 file_size = file.size();
  if (file_size == 0) return false;

  const uchar* mapped = file.map(0, file_size);
  if (!mapped)
  {
    throw std::runtime_error("ArduPilot: failed to memory-map file");
  }

  ArdupilotParser parser(reinterpret_cast<const uint8_t*>(mapped),
                         static_cast<size_t>(file_size));

  for (const auto& [key, series] : parser.getSeriesMap())
  {
    if (series.values.empty()) continue;

    // Replace ASCII '/' in unit with Unicode division slash (U+2215)
    // to prevent PlotJuggler from treating "m/s" as a path separator.
    std::string unit = series.unit;
    for (size_t p = 0; (p = unit.find('/', p)) != std::string::npos; )
    {
      unit.replace(p, 1, "\xe2\x88\x95");
      p += 3;
    }

    const std::string display_key = (show_units && !unit.empty())
        ? key + "(" + unit + ")"
        : key;

    auto& plot = dest.getOrCreateNumeric(display_key);
    for (size_t i = 0; i < series.values.size(); i++)
      plot.pushBack({ series.timestamps[i], series.values[i] });
  }

  auto* dlg = new ArdupilotInfoDialog(parser.getParameters(),
                                      parser.getEmbeddedFiles(),
                                      QApplication::activeWindow());
  dlg->setWindowTitle(
      QString("ArduPilot log: %1")
          .arg(QFileInfo(info->filename).fileName()));
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->restoreSettings();
  dlg->show();

  file.unmap(const_cast<uchar*>(mapped));
  return true;
}
