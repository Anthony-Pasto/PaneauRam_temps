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

void RunPID::task()
{
    using Horloge = std::chrono::steady_clock;
    const std::chrono::milliseconds periode(100);
    const double dtNominal = std::chrono::duration<double>(periode).count();
    auto dernierCalcul = Horloge::now();

    while (enMarche)
    {
        const auto prochainCycle = Horloge::now() + periode;
        Resultat cycle;
        {
            std::lock_guard<std::mutex> verrouPanneau(mutexPanneau);
            if (!enMarche)
                return;

            if (!panneau.read(cycle.lecture))
            {
                std::lock_guard<std::mutex> verrou(mutexEtat);
                resultat.disponible = false;
                resultat.erreur = panneau.lastError();
                return;
            }

            Reglages commandes;
            bool reinitialiser;
            bool reinitialiserTemperature;
            {
                std::lock_guard<std::mutex> verrou(mutexEtat);
                commandes = reglages;
                commandes.automatique = commandes.automatique && !resultat.arretDebordement;
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
                    std::lock_guard<std::mutex> verrou(mutexEtat);
                    resultat.disponible = false;
                    resultat.erreur = panneau.lastError();
                    return;
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
            cycle.automatique = reglages.automatique;
            resultat = cycle;
        }

        std::this_thread::sleep_until(prochainCycle);
    }
}
