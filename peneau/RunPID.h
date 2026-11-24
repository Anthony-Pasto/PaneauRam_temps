#pragma once

#include "Thread.h"
#include "Panneau.h"
#include "Pid.h"
#include <array>
#include <mutex>
#include <string>

class RunPID : public Thread {
public:
    enum class Alarme { DebordementGros, DebordementPetit, NiveauMaxGros,
                       NiveauMaxPetit, ConnexionBalance, ConnexionPowerMeter, Nombre };
    struct Reglages {
        double consigneGros = 50.0;
        double hauterMaxGB = 100.0;
        double hauterMaxPB = 100.0;
        double tempsActiveAlarmeGB = 2.0;
        double tempsActiveAlarmePB = 2.0;
        double tempsIgnorerGB = 2.0;
        double tempsIgnorerPB = 2.0;
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
        bool fermetureAlarmeConfirmee = false;
        bool niveauMaxGrosActif = false;
        bool niveauMaxPetitActif = false;
        std::array<bool, static_cast<std::size_t>(Alarme::Nombre)> alarmesMemorisees{};
        bool arretSerie = false;
        bool sortiesFermees = false;
        std::string erreur;
        PanelReading lecture{};
        double ouvertureGros = 0.0;
        double ouverturePetit = 0.0;
        double ouvertureChaude = 0.0;
        double ouvertureFroide = 0.0;
        TermesPID gros{};
        TermesPID petit{};
        TermesPID temperature{};
        double hauterMaxGB = 100.0;
        bool temperatureActive = false;
        bool automatique = false;

    };

    RunPID(Panel& panneau, std::mutex& mutexPanneau);
    ~RunPID() override;

    void configurer(const Reglages& nouveauxReglages, bool reinitialiser = false);
    Resultat getResultat() const;
    void actualiserAlarme(Alarme alarme, bool active);
    void acquitterAlarme(Alarme alarme);
    void confirmerArretSerie();

protected:
    void task() override;

private:
    struct EtatAlarme {
        bool active = false;
        bool verrouillee = false;
    };
    std::array<EtatAlarme, static_cast<std::size_t>(Alarme::Nombre)> alarmes{};
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
