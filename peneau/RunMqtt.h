#pragma once

#include "Thread.h"
#include <mutex>
#include <string>

class Mqtt;
class RunPID;
class Panel;

class RunMqtt : public Thread {
public:
    struct ValeurRecue {
        bool recue = false;
        bool estCommande = false;
        std::string contenu;

        bool lireConsigne(double& valeur) const;
    };

    struct MisesAJour {
        ValeurRecue consigneTemperature;
        ValeurRecue mode;
        ValeurRecue pompe;
        ValeurRecue valveEC;
        ValeurRecue valveGB;
        ValeurRecue valvePB;
        ValeurRecue valveEF;
        ValeurRecue valveEEC;
        ValeurRecue consigneGros;
        ValeurRecue consignePetit;
        ValeurRecue valveEEF;
        ValeurRecue nivGB;
        ValeurRecue nivPB;
        ValeurRecue tmpGB;
        ValeurRecue tmpPB;
        ValeurRecue alarmePetit;
        ValeurRecue alarmeGros;
    };

    RunMqtt(const RunPID& runPID, Mqtt& mqtt, Panel& panneau);
    ~RunMqtt() override;

    void configurerConsignes(double petit, double temperature);
    bool prendreMisesAJour(MisesAJour& valeurs);

protected:
    void task() override;

private:
    const RunPID& runPID;
    Mqtt& mqtt;
    Panel& panneau;
    std::mutex mutexConsignes;
    double consignePetit = 0.0;
    double consigneTemperature = 0.0;
    MisesAJour enAttente;
    bool misesAJourDisponibles = false;
};
