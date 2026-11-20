#pragma once

#include "Thread.h"
#include "Panneau.h"
#include "Pid.h"
#include <mutex>
#include <string>

class RunPID : public Thread {
public:
    struct Reglages {
        double consigneGros = 50.0;
        double consignePetit = 50.0;
        double consigneTemperature = 25.0;
        bool temperatureActive = false;
        bool automatique = false;
    };

    struct TermesPID {
        double p;
        double i;
        double d;
    };

    struct Resultat {
        bool disponible = false;
        bool arretDebordement = false;
        std::string erreur;
        PanelReading lecture{};
        double ouvertureGros = 0.0;
        double ouverturePetit = 0.0;
        double ouvertureChaude = 0.0;
        double ouvertureFroide = 0.0;
        TermesPID gros{};
        TermesPID petit{};
        TermesPID temperature{};
    };

    RunPID(Panel& panneau, std::mutex& mutexPanneau);
    ~RunPID() override;

    void configurer(const Reglages& nouveauxReglages, bool reinitialiser = false);
    Resultat getResultat() const;
    void confirmerArretDebordement();

protected:
    void task() override;

private:
    // Ce verrou couvre un cycle série complet ou une commande manuelle.
    Panel& panneau;
    std::mutex& mutexPanneau;

    Pid pidGros{2.5, 0.23, 1.2, 0.0, 100.0};
    Pid pidPetit{1.1, 0.17, 0.5, 0.0, 100.0};
    Pid pidTemperature{18.0, 0.25, 5.0, -100.0, 100.0};

    mutable std::mutex mutexEtat;
    Reglages reglages;
    bool resetPIDs = false;
    bool resetTemperature = false;
    Resultat resultat;
};
