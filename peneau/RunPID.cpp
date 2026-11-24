#include "RunPID.h"

#include <chrono>
#include <thread>

RunPID::RunPID(Panel& panneau, std::mutex& mutexPanneau)
    : Thread("RunPID", SCHED_FIFO, 50, 2),
      panneau(panneau),
      mutexPanneau(mutexPanneau)
{
    pidGros.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    pidPetit.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    pidTemperature.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
}

RunPID::~RunPID()
{
    stop();
    join();
}

void RunPID::configurer(const Reglages& nouveauxReglages, bool reinitialiser)
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    resetPIDs = resetPIDs || reinitialiser || (!reglages.automatique && nouveauxReglages.automatique);
    resetTemperature = resetTemperature || (reglages.temperatureActive != nouveauxReglages.temperatureActive);
    reglages = nouveauxReglages;
    resultat.temperatureActive = reglages.temperatureActive;
    resultat.automatique = reglages.automatique &&
                           !resultat.arretDebordement && !resultat.arretSerie;
}

RunPID::Resultat RunPID::getResultat() const
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    return resultat;
}

void RunPID::confirmerArretDebordement()
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    if (!resultat.lecture.debordementGrosBassin && !resultat.lecture.debordementPetitBassin)
        resultat.arretDebordement = false;
}

void RunPID::confirmerArretSerie()
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    if (resultat.disponible)
    {
        reglages.automatique = false;
        resultat.arretSerie = false;
    }
}

void RunPID::task()
{
    using Horloge = std::chrono::steady_clock;
    const std::chrono::milliseconds periode(100);
    const double dtNominal = std::chrono::duration<double>(periode).count();
    auto dernierCalcul = Horloge::now();
    auto prochainCycle = Horloge::now();
    bool fermetureEnAttente = false;

    // Le verrou du panneau est détenu jusqu'à la publication du défaut.
    auto arreterSurErreur = [&] {
        const std::string erreur = panneau.lastError();
        fermetureEnAttente = !panneau.fermerSorties(true);

        std::lock_guard<std::mutex> verrou(mutexEtat);
        resultat.arretSerie = true;
        resultat.sortiesFermees = !fermetureEnAttente;
        resultat.disponible = false;
        resultat.erreur = erreur;
        resultat.automatique = false;
        reglages.automatique = false;
    };

    while (enMarche)
    {
        std::this_thread::sleep_until(prochainCycle);
        if (!enMarche)
            break;

        prochainCycle = Horloge::now() + periode;
        Resultat cycle;
        {
            std::lock_guard<std::mutex> verrouPanneau(mutexPanneau);
            if (!enMarche)
                return;

            if (fermetureEnAttente)
            {
                fermetureEnAttente = !panneau.fermerSorties(true);
                {
                    std::lock_guard<std::mutex> verrou(mutexEtat);
                    resultat.sortiesFermees = !fermetureEnAttente;
                }
                if (fermetureEnAttente)
                {
                    prochainCycle = Horloge::now() + periode;
                    continue;
                }
            }

            if (!panneau.read(cycle.lecture))
            {
                arreterSurErreur();
                continue;
            }

            Reglages commandes;
            bool reinitialiser;
            bool reinitialiserTemperature;
            {
                std::lock_guard<std::mutex> verrou(mutexEtat);
                commandes = reglages;
                commandes.automatique = commandes.automatique &&
                                        !resultat.arretDebordement && !resultat.arretSerie;
                reinitialiser = resetPIDs;
                reinitialiserTemperature = resetTemperature;
                resetPIDs = false;
                resetTemperature = false;
            }

            if (reinitialiser)
            {
                pidGros.reset();
                pidPetit.reset();
            }
            if (reinitialiser || reinitialiserTemperature)
                pidTemperature.reset();

            const auto maintenant = Horloge::now();
            double dt = std::chrono::duration<double>(maintenant - dernierCalcul).count();
            if (reinitialiser) dt = dtNominal;
            if (dt < 0.001) dt = 0.001;
            if (dt > 2.0) dt = 2.0;
            dernierCalcul = maintenant;

            if (enMarche && commandes.automatique &&
                !cycle.lecture.debordementGrosBassin && !cycle.lecture.debordementPetitBassin)
            {
                cycle.ouvertureGros = pidGros.compute(commandes.consigneGros, cycle.lecture.niveauGrosBassin, dt);
                cycle.ouverturePetit = pidPetit.compute(commandes.consignePetit, cycle.lecture.niveauPetitBassin, dt);
                if (commandes.temperatureActive)
                {
                    const double commande = pidTemperature.compute(
                        commandes.consigneTemperature, cycle.lecture.temperaturePetitBassin,
                        reinitialiserTemperature ? dtNominal : dt);
                    // La sortie du PID de température est bornée entre -100 et 100.
                    cycle.ouvertureChaude = 50.0 + commande / 2.0;
                    cycle.ouvertureFroide = 50.0 - commande / 2.0;
                }

                if (!panneau.setValve(Valve::LargeTank, cycle.ouvertureGros) ||
                    !panneau.setValve(Valve::SmallTank, cycle.ouverturePetit) ||
                    !panneau.setValve(Valve::HotWater, cycle.ouvertureChaude) ||
                    !panneau.setValve(Valve::ColdWater, cycle.ouvertureFroide) ||
                    !panneau.setHotWater(cycle.ouvertureChaude > 0.0) ||
                    !panneau.setColdWater(cycle.ouvertureFroide > 0.0))
                {
                    arreterSurErreur();
                    continue;
                }
            }
        }

        cycle.disponible = true;
        cycle.gros = {pidGros.termP(), pidGros.termI(), pidGros.termD()};
        cycle.petit = {pidPetit.termP(), pidPetit.termI(), pidPetit.termD()};
        cycle.temperature = {pidTemperature.termP(), pidTemperature.termI(), pidTemperature.termD()};
        {
            std::lock_guard<std::mutex> verrou(mutexEtat);
            const bool debordement = cycle.lecture.debordementGrosBassin || cycle.lecture.debordementPetitBassin;
            if (debordement)
                reglages.automatique = false;
            // Conserver une alarme même si elle disparaît entre deux lectures de main.
            cycle.arretDebordement = resultat.arretDebordement || debordement;
            cycle.arretSerie = resultat.arretSerie;
            cycle.sortiesFermees = resultat.sortiesFermees;
            cycle.temperatureActive = reglages.temperatureActive;
            cycle.automatique = reglages.automatique &&
                                !cycle.arretDebordement && !cycle.arretSerie;
            resultat = cycle;
        }

    }
}
