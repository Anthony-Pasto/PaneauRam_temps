#pragma once

#include "Thread.h"

class PowerMeter;

class RunAcquisition : public Thread {
public:
    explicit RunAcquisition(PowerMeter& compteur);
    ~RunAcquisition() override;

protected:
    void task() override;

private:
    PowerMeter& compteur;
};
