#pragma once

#include <QDialog>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <vector>
#include "ardupilot_parser.h"

namespace Ui { class ArdupilotInfoDialog; }

class ArdupilotInfoDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ArdupilotInfoDialog(const std::vector<ApParameter>& params,
                               const std::vector<ApEmbeddedFile>& files,
                               QWidget* parent = nullptr);
  ~ArdupilotInfoDialog() override;

  void saveSettings();
  void restoreSettings();

private slots:
  void onSearchChanged(const QString& text);
  void onExport();
  void onExportSelected();
  void onExportAll();

private:
  void exportFilesToFolder(const QList<QModelIndex>& rows);

  Ui::ArdupilotInfoDialog* ui;
  std::vector<ApEmbeddedFile> _embeddedFiles;
};
