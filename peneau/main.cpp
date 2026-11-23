#include "Panneau.h"
#include "Mqtt.h"
#include "Keyboard.h"
#include "PidLog.h"
#include "RunPID.h"
#include "RunMqtt.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <mutex>
#include <string>

static void effacerEcran()
{
    std::printf("\033[3J\033[H\033[2J");
    std::fflush(stdout);
}

static double bornerPourcentage(double valeur)
{
    if (valeur < 0.0)
        return 0.0;
    if (valeur > 100.0)
        return 100.0;
    return valeur;
}

static void afficherBarre(const char *titre, double valeur, double consigne)
{
    valeur = bornerPourcentage(valeur);
    consigne = bornerPourcentage(consigne);

    int remplissage = static_cast<int>((valeur / 100.0) * 20.0 + 0.5);
    int marqueConsigne = static_cast<int>((consigne / 100.0) * 20.0 + 0.5);

    std::printf("%-12s [", titre);
    for (int i = 0; i < 20; ++i)
    {
        if (i == marqueConsigne)
            std::printf("|");
        else if (i < remplissage)
            std::printf("#");
        else
            std::printf("-");
    }
    std::printf("] %3.0f%%  consigne %3.0f%%\n", valeur, consigne);
}

static const std::string TOPIC_POMPE = "RAM/PANNEAU/ETATS/Pompe";

int main(int argc, char* argv[])
{
    const char* portSerie = argc > 1 ? argv[1] : "/dev/pts/2";
    int delaiReponseMs = 200;
    if (argc > 2)
    {
        char* fin = nullptr;
        const long delai = std::strtol(argv[2], &fin, 10);
        if (fin == argv[2] || *fin != '\0' || delai <= 0 || delai >= INT_MAX)
        {
            std::fprintf(stderr, "Délai série invalide : %s\n", argv[2]);
            return 1;
        }
        delaiReponseMs = static_cast<int>(delai);
    }
    RunPID::Reglages reglages;
    
    bool pompeEnMarche = false;

    Mqtt mqtt;
    if (!mqtt.connecter("172.17.15.112", 1883))
    {
        printf("Erreur MQTT : connexion impossible\n");
        return 1;
    }
    if (!mqtt.abonner("RAM/+/CMD/#"))
    {
        std::fprintf(stderr, "Abonnement MQTT impossible\n");
    }
    if (!mqtt.abonner("RAM/+/ACK/#"))
    {
        std::fprintf(stderr, "Échec de l’abonnement à ConsigneGros\n");
    }

    Panel panneau(delaiReponseMs);
    if (!panneau.open(portSerie))
    {
        std::fprintf(stderr, "Erreur d'ouverture du port %s : %s\n", portSerie, panneau.lastError());
        mqtt.deconnecter();
        return 1;
    }

    double ouvertureEauChaude = 0.0;
    double ouvertureEauFroide = 0.0;
    double ouvertureGrandManuelle = 0.0;
    double ouverturePetitManuelle = 0.0;
    std::mutex mutexPanneau;
    RunPID runPIDThread(panneau, mutexPanneau);
    RunMqtt runMqttThread(runPIDThread, mqtt, panneau);
    runMqttThread.configurerConsignes(reglages.consignePetit, reglages.consigneTemperature);

    auto passerEnManuel = [&] {
        reglages.automatique = false;
        runPIDThread.configurer(reglages);
    };
    auto fermerSorties = [&](bool arreterPompe) {
        std::lock_guard<std::mutex> verrou(mutexPanneau);
        bool ok = !arreterPompe || panneau.setPump(false);
        ok = panneau.setHotWater(false) && ok;
        ok = panneau.setColdWater(false) && ok;
        ok = panneau.setValve(Valve::LargeTank, 0.0) && ok;
        ok = panneau.setValve(Valve::SmallTank, 0.0) && ok;
        ok = panneau.setValve(Valve::HotWater, 0.0) && ok;
        ok = panneau.setValve(Valve::ColdWater, 0.0) && ok;
        ouvertureGrandManuelle = 0.0;
        ouverturePetitManuelle = 0.0;
        ouvertureEauChaude = 0.0;
        ouvertureEauFroide = 0.0;
        if (!ok)
            std::fprintf(stderr, "Erreur de fermeture du panneau : %s\n", panneau.lastError());
        return ok;
    };

    if (!runPIDThread.start(true))
    {
        fermerSorties(true);
        return 1;
    }

    if (!runMqttThread.start())
    {
        runPIDThread.stop();
        runPIDThread.join();
        fermerSorties(true);
        return 1;
    }

    Keyboard clavier;

    enum class Cible
    {
        NiveauGros,
        NiveauPetit,
        TempPetit
    };
    Cible cible = Cible::NiveauGros;
    enum class ValveManuelle
    {
        Grand,
        Petit,
        Chaude,
        Froide
    };
    ValveManuelle valveManuelle = ValveManuelle::Grand;
    std::string saisie;

    using Horloge = std::chrono::steady_clock;
    auto prochainAffichage = Horloge::now();
    bool debordementTraite = false;
    std::string erreurPanneau;

    std::printf("\033[?25l");
    effacerEcran();

    PidLog pidLog;
    if (!pidLog.isOpen())
    {
        printf("Attention : impossible de creer les trois fichiers PID CSV\n");
    }

    while (true)
    {
       
   
        const char touche = clavier.kbhit(100) ? clavier.getch() : '\0';
        if (touche == 'q' || touche == 'Q')
            break;

        const RunPID::Resultat resultat = runPIDThread.getResultat();
        if (!resultat.erreur.empty())
        {
            erreurPanneau = resultat.erreur;
            break;
        }
        const PanelReading& lecture = resultat.lecture;
        const bool debordement = resultat.arretDebordement;
        if (debordement)
        {
            if (!debordementTraite)
            {
                passerEnManuel();
                pompeEnMarche = false;
                debordementTraite = fermerSorties(true);
                mqtt.envoyer(TOPIC_POMPE, 0.0);
            }
            if (debordementTraite && !lecture.debordementGrosBassin && !lecture.debordementPetitBassin)
                runPIDThread.confirmerArretDebordement();
        }
        else
        {
            debordementTraite = false;
        }

        RunMqtt::MisesAJour valeurs;
        if (runMqttThread.prendreMisesAJour(valeurs) && resultat.disponible && !debordement)
        {
            const bool gros = valeurs.consigneGros.lireConsigne(reglages.consigneGros);
            const bool petit = valeurs.consignePetit.lireConsigne(reglages.consignePetit);
            const bool temperature = valeurs.consigneTemperature.lireConsigne(reglages.consigneTemperature);
            auto commandeEst = [](const RunMqtt::ValeurRecue& valeur, const char* texte) {
                return valeur.recue && valeur.estCommande && valeur.contenu == texte;
            };
            const bool activerAuto = !reglages.automatique && commandeEst(valeurs.mode, "AUTO");
            if (commandeEst(valeurs.mode, "MANUAL") || commandeEst(valeurs.mode, "MANUEL"))
            {
                passerEnManuel();
                fermerSorties(false);
            }
            else if (activerAuto)
            {
                reglages.automatique = true;
                runPIDThread.configurer(reglages, true);
            }
            else if (gros || petit || temperature)
            {
                runPIDThread.configurer(reglages);
            }
            if (gros || petit || temperature)
                runMqttThread.configurerConsignes(reglages.consignePetit, reglages.consigneTemperature);

            const bool pompeOn = commandeEst(valeurs.pompe, "ON") || commandeEst(valeurs.pompe, "1");
            const bool pompeOff = commandeEst(valeurs.pompe, "OFF") || commandeEst(valeurs.pompe, "0");
            if (pompeOn || pompeOff)
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                if (panneau.setPump(pompeOn))
                    pompeEnMarche = pompeOn;
            }

            if (!reglages.automatique)
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                auto reglerValve = [&](const RunMqtt::ValeurRecue& valeur, Valve valve, double& ouverture) {
                    double demande;
                    if (valeur.lireConsigne(demande) && panneau.setValve(valve, demande))
                        ouverture = demande;
                };
                reglerValve(valeurs.valveGB, Valve::LargeTank, ouvertureGrandManuelle);
                reglerValve(valeurs.valvePB, Valve::SmallTank, ouverturePetitManuelle);
                reglerValve(valeurs.valveEC, Valve::HotWater, ouvertureEauChaude);
                reglerValve(valeurs.valveEF, Valve::ColdWater, ouvertureEauFroide);

                if (commandeEst(valeurs.valveEEC, "ON") || commandeEst(valeurs.valveEEC, "OFF"))
                    panneau.setHotWater(valeurs.valveEEC.contenu == "ON");
                if (commandeEst(valeurs.valveEEF, "ON") || commandeEst(valeurs.valveEEF, "OFF"))
                    panneau.setColdWater(valeurs.valveEEF.contenu == "ON");
            }
        }

        if (touche != '\0' && !debordement && resultat.disponible)
        {
            if (!reglages.automatique && touche == '1')
            {
                valveManuelle = ValveManuelle::Grand;
            }
            else if (!reglages.automatique && touche == '2')
            {
                valveManuelle = ValveManuelle::Petit;
            }
            else if (!reglages.automatique && touche == '3')
            {
                valveManuelle = ValveManuelle::Chaude;
            }
            else if (!reglages.automatique && touche == '4')
            {
                valveManuelle = ValveManuelle::Froide;
            }
            else if (!reglages.automatique && (touche == '+' || touche == '='))
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                if (valveManuelle == ValveManuelle::Grand)
                {
                    ouvertureGrandManuelle = bornerPourcentage(ouvertureGrandManuelle + 10.0);
                    panneau.setValve(Valve::LargeTank, ouvertureGrandManuelle);
                }
                else if (valveManuelle == ValveManuelle::Petit)
                {
                    ouverturePetitManuelle = bornerPourcentage(ouverturePetitManuelle + 10.0);
                    panneau.setValve(Valve::SmallTank, ouverturePetitManuelle);
                }
                else if (valveManuelle == ValveManuelle::Chaude)
                {
                    ouvertureEauChaude = bornerPourcentage(ouvertureEauChaude + 10.0);
                    panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                }
                else
                {
                    ouvertureEauFroide = bornerPourcentage(ouvertureEauFroide + 10.0);
                    panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                }
            }
            else if (!reglages.automatique && touche == '-')
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                if (valveManuelle == ValveManuelle::Grand)
                {
                    ouvertureGrandManuelle = bornerPourcentage(ouvertureGrandManuelle - 10.0);
                    panneau.setValve(Valve::LargeTank, ouvertureGrandManuelle);
                }
                else if (valveManuelle == ValveManuelle::Petit)
                {
                    ouverturePetitManuelle = bornerPourcentage(ouverturePetitManuelle - 10.0);
                    panneau.setValve(Valve::SmallTank, ouverturePetitManuelle);
                }
                else if (valveManuelle == ValveManuelle::Chaude)
                {
                    ouvertureEauChaude = bornerPourcentage(ouvertureEauChaude - 10.0);
                    panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                }
                else
                {
                    ouvertureEauFroide = bornerPourcentage(ouvertureEauFroide - 10.0);
                    panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                }
            }
            else if (touche == 'g' || touche == 'G')
            {
                cible = Cible::NiveauGros;
                saisie.clear();
            }
            else if (touche == 's' || touche == 'S')
            {
                cible = Cible::NiveauPetit;
                saisie.clear();
            }
            else if (touche == 't' || touche == 'T')
            {
                reglages.temperatureActive = !reglages.temperatureActive;
                runPIDThread.configurer(reglages);
                saisie.clear();
            }
            else if (touche == 'u' || touche == 'U')
            {
                cible = Cible::TempPetit;
                saisie.clear();
            }
            else if (touche >= '0' && touche <= '9')
            {
                if (saisie.size() < 3)
                    saisie += touche;
            }
            else if (touche == 127 || touche == '\b')
            {
                if (!saisie.empty())
                    saisie.pop_back();
            }
            else if (touche == '\r' || touche == '\n')
            {
                if (!saisie.empty())
                {
                    int valeur = std::stoi(saisie);
                    if (valeur < 0)
                        valeur = 0;
                    if (valeur > 100)
                        valeur = 100;

                    switch (cible)
                    {
                    case Cible::NiveauGros:
                        reglages.consigneGros = valeur;
                        break;
                    case Cible::NiveauPetit:
                        reglages.consignePetit = valeur;
                        break;
                    case Cible::TempPetit:
                        reglages.consigneTemperature = valeur;
                        break;
                    }

                    runPIDThread.configurer(reglages);
                    runMqttThread.configurerConsignes(reglages.consignePetit, reglages.consigneTemperature);
                    saisie.clear();
                }
            }
            else if (touche == 'a' || touche == 'A')
            {
                reglages.automatique = true;
                runPIDThread.configurer(reglages, true);
            }
            else if (touche == 'm' || touche == 'M')
            {
                passerEnManuel();
                fermerSorties(false);
            }
            else if (touche == 'p' || touche == 'P')
            {
                pompeEnMarche = !pompeEnMarche;
                {
                    std::lock_guard<std::mutex> verrou(mutexPanneau);
                    panneau.setPump(pompeEnMarche);
                }
                mqtt.envoyer(TOPIC_POMPE, pompeEnMarche ? 1.0 : 0.0);
            }
            else if (touche == 'o' || touche == 'O')
            {
                passerEnManuel();
                pompeEnMarche = false;
                fermerSorties(true);
                mqtt.envoyer(TOPIC_POMPE, 0.0);
            }
            else if (touche == 'l' || touche == 'L')
            {
                // Ouverture manuelle des valves
                passerEnManuel();
                double ouverture = 0.0;
                printf("Ouverture manuelle (0-100) : ");
                if (scanf("%lf", &ouverture) == 1)
                {
                    ouverture = bornerPourcentage(ouverture);
                    std::lock_guard<std::mutex> verrou(mutexPanneau);
                    panneau.setValve(Valve::LargeTank, ouverture);
                    ouvertureGrandManuelle = ouverture;
                }
                printf("Appuyez sur Entrée pour continuer...");
                getchar(); // Pour consommer le '\n' restant
                getchar();
            }
            else if (touche == 'j' || touche == 'J')
            {
                // Ouverture manuelle des valves
                passerEnManuel();
                double ouverture = 0.0;
                printf("Ouverture manuelle (0-100) : ");
                if (scanf("%lf", &ouverture) == 1)
                {
                    ouverture = bornerPourcentage(ouverture);
                    std::lock_guard<std::mutex> verrou(mutexPanneau);
                    panneau.setValve(Valve::SmallTank, ouverture);
                    ouverturePetitManuelle = ouverture;
                }
                printf("Appuyez sur Entrée pour continuer...");
                getchar(); // Pour consommer le '\n' restant
                getchar();
            }
            else if (!reglages.automatique && (touche == 'h' || touche == 'H'))
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                valveManuelle = ValveManuelle::Chaude;
                ouvertureEauChaude = (ouvertureEauChaude > 0.0) ? 0.0 : 100.0;
                panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                panneau.setHotWater(ouvertureEauChaude > 0.0);
            }
            else if (!reglages.automatique && (touche == 'f' || touche == 'F'))
            {
                std::lock_guard<std::mutex> verrou(mutexPanneau);
                valveManuelle = ValveManuelle::Froide;
                ouvertureEauFroide = (ouvertureEauFroide > 0.0) ? 0.0 : 100.0;
                panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                panneau.setColdWater(ouvertureEauFroide > 0.0);
            }
        }

        const auto maintenant = Horloge::now();
        if (maintenant < prochainAffichage)
            continue;
        prochainAffichage = maintenant + std::chrono::milliseconds(100);
#
        if (!resultat.disponible)
        {
            effacerEcran();
            std::puts("En attente de la première lecture du panneau...");
            std::fflush(stdout);
            continue;
        }

        const double ouvertureGros = reglages.automatique ? resultat.ouvertureGros : ouvertureGrandManuelle;
        const double ouverturePetit = reglages.automatique ? resultat.ouverturePetit : ouverturePetitManuelle;
        if (reglages.automatique)
        {
            ouvertureEauChaude = resultat.ouvertureChaude;
            ouvertureEauFroide = resultat.ouvertureFroide;
        }
        pidLog.enregistrer(
            lecture.niveauGrosBassin,
            reglages.consigneGros,
            lecture.niveauPetitBassin,
            reglages.consignePetit,
            lecture.temperaturePetitBassin,
            reglages.consigneTemperature,
            ouvertureGros,
            ouverturePetit,
            ouvertureEauChaude,
            ouvertureEauFroide,
            resultat.gros.p, resultat.gros.i, resultat.gros.d,
            resultat.petit.p, resultat.petit.i, resultat.petit.d,
            resultat.temperature.p, resultat.temperature.i, resultat.temperature.d);

        effacerEcran();
        // fait par ai
        std::printf("\033[1;36m+============================================================+\033[0m\n");
        std::printf("\033[1;36m|                 PANNEAU RAM - CONTROLE                     |\033[0m\n");
        std::printf("\033[1;36m+============================================================+\033[0m\n");

        std::printf(" MODE : \033[1;%sm%-12s\033[0m",
                    reglages.automatique ? "32" : "33",
                    reglages.automatique ? "AUTOMATIQUE" : "MANUEL");
        std::printf(" POMPE : \033[1;%sm%-3s\033[0m\n",
                    pompeEnMarche ? "32" : "31",
                    pompeEnMarche ? "ON" : "OFF");

        std::printf(" TEMPERATURE : \033[1;%sm%-3s\033[0m",
                    reglages.temperatureActive ? "32" : "31",
                    reglages.temperatureActive ? "ON" : "OFF");
        std::printf(" DEBORDEMENT : ");
        if (debordement)
            std::printf("\033[1;31mALERTE\033[0m\n");
        else
            std::printf("\033[1;32mNORMAL\033[0m\n");

        std::printf("\n--- NIVEAUX ET CONSIGNES ----------------------------------\n");
        afficherBarre("Gros bassin", lecture.niveauGrosBassin, reglages.consigneGros);
        afficherBarre("Petit bassin", lecture.niveauPetitBassin, reglages.consignePetit);
        std::printf("     Grand valve = %3.0f %%   |   Petit valve = %3.0f %%\n",
                    ouvertureGros, ouverturePetit);

        std::printf("\n--- TEMPERATURE PETIT BASSIN -----------------------------\n");
        std::printf("  Actuelle : %3.0f C   Cible : %3.0f C\n",
                    lecture.temperaturePetitBassin, reglages.consigneTemperature);
        std::printf("  Eau chaude : %3.0f %%   Eau froide : %3.0f %%\n",
                    ouvertureEauChaude, ouvertureEauFroide);

        std::printf("\n--- COMMANDE ------------------------------------------------\n");
        const char *nomCible = "NIV. GROS";
        if (cible == Cible::NiveauPetit)
            nomCible = "NIV. PETIT";
        if (cible == Cible::TempPetit)
            nomCible = "TEMP PETIT";
        std::printf("  Cible : %-12s Valeur saisie : %s\n",
                    nomCible,
                    saisie.empty() ? "---" : saisie.c_str());

        const char *nomValve = "GROS";
        if (valveManuelle == ValveManuelle::Petit)
            nomValve = "PETIT";
        if (valveManuelle == ValveManuelle::Chaude)
            nomValve = "EAU CHAUDE";
        if (valveManuelle == ValveManuelle::Froide)
            nomValve = "EAU FROIDE";

        std::printf("  Valve manuelle : %-10s  [1 Gros] [2 Petit] [3 Chaude] [4 Froide]\n",
                    nomValve);
        std::printf("  Ouvertures : G %3.0f%% | P %3.0f%% | Chaude %3.0f%% | Froide %3.0f%%\n",
                    ouvertureGrandManuelle,
                    ouverturePetitManuelle,
                    ouvertureEauChaude,
                    ouvertureEauFroide);

        std::printf("\n--- TOUCHES -------------------------------------------------\n");
        std::printf("  G/S Niveaux  |  U Temperature  |  T Activer temperature\n");
        std::printf("  0-100 + Entree : appliquer  |  A Auto  |  M Manuel\n");
        std::printf("  P Pompe  |  H Chaude  |  F Froide  |  +/- Ouvrir/Fermer 10%%\n");
        std::printf("  O Arret complet  |  Q Quitter\n");
        std::printf("\033[1;36m+============================================================+\033[0m\n");
        // fait par ai
        std::fflush(stdout);
    }

    runMqttThread.stop();
    runPIDThread.stop();
    runMqttThread.join();
    runPIDThread.join();
    const bool sortiesFermees = fermerSorties(true);
    mqtt.envoyer(TOPIC_POMPE, 0.0);
    panneau.close();
    mqtt.deconnecter();

    std::printf("\033[?25h");
    std::fflush(stdout);
    if (!erreurPanneau.empty())
        std::fprintf(stderr, "Erreur RunPID sur %s : %s\n", portSerie, erreurPanneau.c_str());
    return erreurPanneau.empty() && sortiesFermees ? 0 : 1;
}
