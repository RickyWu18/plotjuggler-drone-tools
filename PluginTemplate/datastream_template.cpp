#include "datastream_template.h"
#include <chrono>
#include <cmath>
#include <mutex>

bool DataStreamTemplate::start(QStringList*)
{
    dataMap().addNumeric("drone/template/signal");

    _running = true;
    _thread = std::thread([this] { loop(); });
    return true;
}

void DataStreamTemplate::shutdown()
{
    _running = false;
    if (_thread.joinable())
        _thread.join();
}

void DataStreamTemplate::loop()
{
    using namespace std::chrono;

    while (_running)
    {
        auto prev = high_resolution_clock::now();

        double stamp = duration_cast<duration<double>>(
            high_resolution_clock::now().time_since_epoch()).count();

        {
            std::lock_guard<std::mutex> lock(mutex());
            dataMap().numeric.at("drone/template/signal")
                .pushBack({stamp, std::sin(stamp)});
        }

        emit dataReceived();
        std::this_thread::sleep_until(prev + milliseconds(20)); // 50 Hz
    }
}
