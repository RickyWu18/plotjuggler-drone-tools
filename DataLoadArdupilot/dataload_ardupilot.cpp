#include "dataload_ardupilot.h"
#include "ardupilot_parser.h"
#include "ardupilot_info_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QProgressBar>
#include <QProgressDialog>
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
  bool load_files = pref.value("load_files", true).toBool();

  {
    QDialog dlg;
    dlg.setWindowTitle("ArduPilot Load Settings");
    auto* layout    = new QVBoxLayout(&dlg);
    auto* cb_units  = new QCheckBox("Append units to series names (e.g. Roll \xe2\x86\x92 Roll(deg))", &dlg);
    auto* cb_files  = new QCheckBox("Load embedded FILE messages (may be slow for large logs)", &dlg);
    auto* buttons   = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    cb_units->setChecked(show_units);
    cb_files->setChecked(load_files);
    layout->addWidget(cb_units);
    layout->addWidget(cb_files);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    show_units = cb_units->isChecked();
    load_files = cb_files->isChecked();
    pref.setValue("show_units", show_units);
    pref.setValue("load_files", load_files);
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

  QWidget* main_window = QApplication::activeWindow();

  QProgressDialog progress_dialog;
  progress_dialog.setWindowTitle("Loading ArduPilot log");
  progress_dialog.setLabelText("Decoding log file...");
  progress_dialog.setWindowModality(Qt::ApplicationModal);
  progress_dialog.setRange(0, 100);
  progress_dialog.setAutoClose(false);
  progress_dialog.setAutoReset(false);
  progress_dialog.show();

  if (auto* bar = progress_dialog.findChild<QProgressBar*>())
  {
    bar->setTextVisible(true);
    bar->setAlignment(Qt::AlignCenter);
  }

  // Parsing phase: 0–50%
  ArdupilotParser parser(
      reinterpret_cast<const uint8_t*>(mapped),
      static_cast<size_t>(file_size),
      load_files,
      [&](size_t pos, size_t total) -> bool {
        progress_dialog.setValue(static_cast<int>(50.0 * pos / total));
        QApplication::processEvents();
        return !progress_dialog.wasCanceled();
      });

  if (progress_dialog.wasCanceled())
  {
    file.unmap(const_cast<uchar*>(mapped));
    return false;
  }

  // Write phase: 50–100%
  progress_dialog.setLabelText("Writing data to PlotJuggler...");
  progress_dialog.setValue(50);
  QApplication::processEvents();

  const auto& series_map = parser.getSeriesMap();
  size_t total_samples = 0;
  for (const auto& [key, series] : series_map)
    total_samples += series.values.size();

  size_t written = 0;
  for (const auto& [key, series] : series_map)
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

    written += series.values.size();
    if (total_samples > 0)
      progress_dialog.setValue(50 + static_cast<int>(50.0 * written / total_samples));
    QApplication::processEvents();

    if (progress_dialog.wasCanceled())
    {
      dest.clear();
      file.unmap(const_cast<uchar*>(mapped));
      return false;
    }
  }

  auto* dlg = new ArdupilotInfoDialog(parser.getParameters(),
                                      parser.getEmbeddedFiles(),
                                      main_window);
  dlg->setWindowTitle(
      QString("ArduPilot log: %1")
          .arg(QFileInfo(info->filename).fileName()));
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->restoreSettings();
  dlg->show();

  progress_dialog.setValue(100);
  progress_dialog.close();

  file.unmap(const_cast<uchar*>(mapped));
  return true;
}
