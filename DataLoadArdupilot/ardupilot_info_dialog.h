/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

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
                               const std::vector<ApLogMessage>& msgs,
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
