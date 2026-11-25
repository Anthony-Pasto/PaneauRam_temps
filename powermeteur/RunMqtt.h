#pragma once

#include "Thread.h"

class Mqtt;
class PowerMeter;
struct PowerMeterReading;

class RunMqtt : public Thread {
public:
    RunMqtt(Mqtt& mqtt, PowerMeter& compteur);
    ~RunMqtt() override;

protected:
    void task() override;

private:
    void publier(const PowerMeterReading& lecture);
    Mqtt& mqtt;
    PowerMeter& compteur;
};
