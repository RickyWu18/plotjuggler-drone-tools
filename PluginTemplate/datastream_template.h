#pragma once

#include <QtPlugin>
#include <thread>
#include "PlotJuggler/datastreamer_base.h"

class DataStreamTemplate : public PJ::DataStreamer
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "facontidavide.PlotJuggler3.DataStreamer")
  Q_INTERFACES(PJ::DataStreamer)

public:
  DataStreamTemplate() = default;
  ~DataStreamTemplate() override { shutdown(); }

  bool start(QStringList* args) override;
  void shutdown() override;
  bool isRunning() const override { return _running; }

  const char* name() const override { return "Drone Template"; }
  bool isDebugPlugin() override { return true; }

  bool xmlSaveState(QDomDocument&, QDomElement&) const override { return true; }
  bool xmlLoadState(const QDomElement&) override { return true; }

private:
  void loop();

  std::thread _thread;
  bool _running = false;
};
