#pragma once

#include "Thread.h"
#include <mutex>

class Mqtt;
class RunPID;
class Panel;

class RunMqtt : public Thread {
public:
    RunMqtt(const RunPID& runPID, Mqtt& mqtt, Panel& panneau);
    ~RunMqtt() override;

    void configurerConsignes(double petit, double temperature);
    bool prendreConsigneTemperature(double& valeur);

protected:
    void task() override;

private:
    const RunPID& runPID;
    Mqtt& mqtt;
    Panel& panneau;
    std::mutex mutexConsignes;
    double consignePetit = 0.0;
    double consigneTemperature = 0.0;
    bool nouvelleConsigneTemperature = false;
double temperatureRecue = 0.0;
};
