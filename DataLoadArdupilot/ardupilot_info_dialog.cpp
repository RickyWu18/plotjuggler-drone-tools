#include "ardupilot_info_dialog.h"
#include "ui_ardupilot_info_dialog.h"

#include <QFileDialog>
#include <QFile>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidgetItem>
#include <QTextStream>

ArdupilotInfoDialog::ArdupilotInfoDialog(const std::vector<ApParameter>& params,
                                         QWidget* parent)
  : QDialog(parent), ui(new Ui::ArdupilotInfoDialog)
{
  ui->setupUi(this);

  auto* table = ui->tableParams;
  table->setRowCount(static_cast<int>(params.size()));
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  for (int row = 0; row < static_cast<int>(params.size()); row++)
  {
    table->setItem(row, 0,
        new QTableWidgetItem(QString::fromStdString(params[row].name)));
    table->setItem(row, 1,
        new QTableWidgetItem(QString::number(params[row].value, 'g', 8)));
  }

  table->sortItems(0);

  connect(ui->searchEdit, &QLineEdit::textChanged, this, &ArdupilotInfoDialog::onSearchChanged);
  connect(ui->btnExport,  &QPushButton::clicked,   this, &ArdupilotInfoDialog::onExport);
  connect(ui->buttonBox,  &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ArdupilotInfoDialog::~ArdupilotInfoDialog()
{
  saveSettings();
  delete ui;
}

void ArdupilotInfoDialog::onSearchChanged(const QString& text)
{
  auto* table = ui->tableParams;
  QRegularExpression re(text, QRegularExpression::CaseInsensitiveOption);
  const bool valid = re.isValid();
  for (int row = 0; row < table->rowCount(); row++)
  {
    auto* item = table->item(row, 0);
    const bool match = !item ? false
                     : text.isEmpty() ? true
                     : valid && re.match(item->text()).hasMatch();
    table->setRowHidden(row, !match);
  }
}

void ArdupilotInfoDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "Export Parameters", QString(), "Parameter files (*.param)");
  if (path.isEmpty()) return;

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    QMessageBox::warning(this, "Export Failed",
                         "Could not open file for writing:\n" + path);
    return;
  }

  QTextStream out(&file);
  auto* table = ui->tableParams;
  for (int row = 0; row < table->rowCount(); row++)
  {
    if (table->isRowHidden(row)) continue;
    const QString name  = table->item(row, 0)->text();
    const QString value = table->item(row, 1)->text();
    out << name << "," << value << "\n";
  }
}

void ArdupilotInfoDialog::saveSettings()
{
  QSettings settings;
  settings.setValue("ArdupilotParamsDialog/geometry", saveGeometry());
  settings.setValue("ArdupilotParamsDialog/tableState",
                    ui->tableParams->horizontalHeader()->saveState());
}

void ArdupilotInfoDialog::restoreSettings()
{
  QSettings settings;
  restoreGeometry(settings.value("ArdupilotParamsDialog/geometry").toByteArray());
  ui->tableParams->horizontalHeader()->restoreState(
      settings.value("ArdupilotParamsDialog/tableState").toByteArray());
}
