#pragma once

#include "Thread.h"
#include "Panneau.h"
#include "Mqtt.h"
#include <mutex>
#include <string>

class RunMqtt : public Thread {
public:
   
    RunMqtt(Panel& panneau, std::mutex& mutexPanneau);
    ~RunMqtt() override;

    void configurer(const Reglages& nouveauxReglages, bool reinitialiser = false);
    Resultat getResultat() const;

protected:
    void task() override;

private:
    
    Panel& panneau;
    std::mutex& mutexPanneau;

   
    mutable std::mutex mutexEtat;
    Reglages reglages;
    
    Resultat resultat;
};
