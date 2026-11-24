#include "RunPID.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace {
using Horloge = std::chrono::steady_clock;

struct TemporisationNiveauHaut {
    bool active = false;
    bool transitionEnCours = false;
    Horloge::time_point debutTransition{};

    bool actualiser(bool auSeuil, Horloge::time_point instant,
                    double delaiActivation, double delaiDesactivation)
    {
        if (auSeuil == active)
        {
            transitionEnCours = false;
            return active;
        }

        if (!transitionEnCours)
        {
            debutTransition = instant;
            transitionEnCours = true;
        }

        const double delai = auSeuil ? delaiActivation : delaiDesactivation;
        if (std::chrono::duration<double>(instant - debutTransition).count() >= delai)
        {
            active = auSeuil;
            transitionEnCours = false;
        }
        return active;
    }
};
}

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
    resultat.hauterMaxGB = reglages.hauterMaxGB;
    if (resultat.arretDebordement || resultat.arretSerie)
        reglages.automatique = false;
    resultat.temperatureActive = reglages.temperatureActive;
    resultat.automatique = reglages.automatique &&
                           !resultat.arretDebordement && !resultat.arretSerie;
}

RunPID::Resultat RunPID::getResultat() const
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    Resultat copie = resultat;
    for (std::size_t i = 0; i < alarmes.size(); ++i)
        copie.alarmesMemorisees[i] = alarmes[i].verrouillee;
    copie.alarmesMemorisees[static_cast<std::size_t>(Alarme::NiveauMaxGros)] =
        resultat.niveauMaxGrosActif;
    copie.alarmesMemorisees[static_cast<std::size_t>(Alarme::NiveauMaxPetit)] =
        resultat.niveauMaxPetitActif;
    return copie;
}

void RunPID::actualiserAlarme(Alarme alarme, bool active)
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    auto& etat = alarmes[static_cast<std::size_t>(alarme)];
    etat.active = active;
    if (active && alarme != Alarme::NiveauMaxGros && alarme != Alarme::NiveauMaxPetit)
    {
        if (!resultat.arretDebordement)
            resultat.fermetureAlarmeConfirmee = false;
        etat.verrouillee = true;
        resultat.arretDebordement = true;
        reglages.automatique = false;
        resultat.automatique = false;
    }
}

void RunPID::acquitterAlarme(Alarme alarme)
{
    std::lock_guard<std::mutex> verrou(mutexEtat);
    if (alarme == Alarme::NiveauMaxGros || alarme == Alarme::NiveauMaxPetit)
        return;

    auto& etat = alarmes[static_cast<std::size_t>(alarme)];
    if (etat.active || !resultat.fermetureAlarmeConfirmee ||
        resultat.arretSerie || !resultat.disponible)
        return;

    etat.verrouillee = false;
    resultat.arretDebordement = std::any_of(alarmes.begin(), alarmes.end(),
        [](const EtatAlarme& autre) { return autre.verrouillee; });
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
    const std::chrono::milliseconds periode(100);
    const double dtNominal = std::chrono::duration<double>(periode).count();
    auto dernierCalcul = Horloge::now();
    auto prochainCycle = Horloge::now();
    bool fermetureEnAttente = false;
    TemporisationNiveauHaut niveauHautGros;
    TemporisationNiveauHaut niveauHautPetit;

    // Le verrou du panneau est détenu jusqu'à la publication du défaut.
    auto arreterSurErreur = [&] {
        niveauHautGros.transitionEnCours = false;
        niveauHautPetit.transitionEnCours = false;
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

    auto fermerSiAlarme = [&] {
        {
            std::lock_guard<std::mutex> verrou(mutexEtat);
            if (!resultat.arretDebordement || resultat.fermetureAlarmeConfirmee)
                return true;
        }
        const bool ok = panneau.fermerSorties(true);
        std::lock_guard<std::mutex> verrou(mutexEtat);
        resultat.fermetureAlarmeConfirmee = ok;
        return ok;
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

            if (!fermerSiAlarme())
            {
                niveauHautGros.transitionEnCours = false;
                niveauHautPetit.transitionEnCours = false;
                prochainCycle = Horloge::now() + periode;
                continue;
            }

            if (!panneau.read(cycle.lecture))
            {
                arreterSurErreur();
                continue;
            }

            actualiserAlarme(Alarme::DebordementGros, cycle.lecture.debordementGrosBassin);
            actualiserAlarme(Alarme::DebordementPetit, cycle.lecture.debordementPetitBassin);
            {
                std::lock_guard<std::mutex> verrou(mutexEtat);
                const auto instantLecture = Horloge::now();
                resultat.niveauMaxGrosActif = niveauHautGros.actualiser(
                    cycle.lecture.niveauGrosBassin >= reglages.hauterMaxGB, instantLecture,
                    reglages.tempsActiveAlarmeGB, reglages.tempsIgnorerGB);
                resultat.niveauMaxPetitActif = niveauHautPetit.actualiser(
                    cycle.lecture.niveauPetitBassin >= reglages.hauterMaxPB, instantLecture,
                    reglages.tempsActiveAlarmePB, reglages.tempsIgnorerPB);
            }
            if (!fermerSiAlarme())
            {
                prochainCycle = Horloge::now() + periode;
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
            cycle.arretDebordement = resultat.arretDebordement;
            cycle.niveauMaxGrosActif = resultat.niveauMaxGrosActif;
            cycle.niveauMaxPetitActif = resultat.niveauMaxPetitActif;
            cycle.fermetureAlarmeConfirmee = resultat.fermetureAlarmeConfirmee;
            cycle.arretSerie = resultat.arretSerie;
            cycle.sortiesFermees = resultat.sortiesFermees;
            cycle.temperatureActive = reglages.temperatureActive;
            cycle.hauterMaxGB = reglages.hauterMaxGB;
            cycle.automatique = reglages.automatique &&
                                !cycle.arretDebordement && !cycle.arretSerie;
            resultat = cycle;
        }

    }
}
