#pragma once

#include <QDialog>
#include <QString>
#include <vector>
#include "ardupilot_parser.h"

namespace Ui { class ArdupilotInfoDialog; }

class ArdupilotInfoDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ArdupilotInfoDialog(const std::vector<ApParameter>& params,
                               QWidget* parent = nullptr);
  ~ArdupilotInfoDialog() override;

  void saveSettings();
  void restoreSettings();

private slots:
  void onSearchChanged(const QString& text);
  void onExport();

private:
  Ui::ArdupilotInfoDialog* ui;
};
