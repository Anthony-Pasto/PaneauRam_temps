// creer par ai
#pragma once

#include <chrono>
#include <fstream>
#include <string>

class PidLog {
public:
    PidLog();
    ~PidLog();

    bool isOpen() const;

    void enregistrer(
        double niveauGrosPct,
        double consigneGrosPct,
        double niveauPetitPct,
        double consignePetitPct,
        double temperaturePetitC,
        double consigneTemperaturePetitC,
        double commandeValveGrosPct,
        double commandeValvePetitPct,
        double commandeValveHotPct,
        double commandeValveColdPct,
        double gbP, double gbI, double gbD,
        double pbP, double pbI, double pbD,
        double tmpP, double tmpI, double tmpD
    );

    void close();

private:
    std::ofstream fichierGros;
    std::ofstream fichierPetit;
    std::ofstream fichierTemperature;
    std::chrono::steady_clock::time_point debut;
};
// creer par ai