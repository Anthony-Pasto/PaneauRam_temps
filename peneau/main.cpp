#include "Panneau.h"
#include "Mqtt.h"
#include "Keyboard.h"
#include "PidLog.h"
#include "RunPID.h"
#include "RunMqtt.h"

#include <chrono>
#include <cstdio>
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

// Remplace ces chaînes par tes propres noms de topics avant utilisation.
static const std::string TOPIC_CONSIGNE_PETIT = "RAM/PANNEAU/CMD/ConsTmpPB";
static const std::string TOPIC_CONSIGNE_TEMP_PETIT = "RAM/PANNEAU/CMD/ConsTempPB";
static const std::string TOPIC_NIVEAU_GROS = "RAM/PANNEAU/ETATS/NivGB";
static const std::string TOPIC_NIVEAU_PETIT = "RAM/PANNEAU/ETATS/NivPB";
static const std::string TOPIC_TEMP_GROS = "RAM/PANNEAU/ETATS/TmpGB";
static const std::string TOPIC_TEMP_PETIT = "RAM/PANNEAU/ETATS/TmpPB";

static const std::string TOPIC_VALVE_LARGE = "RAM/PANNEAU/ETATS/ValveGB";
static const std::string TOPIC_VALVE_HOT = "RAM/PANNEAU/ETATS/ValveEC";
static const std::string TOPIC_VALVE_SMALL = "RAM/PANNEAU/ETATS/ValvePB";
static const std::string TOPIC_VALVE_COLD = "RAM/PANNEAU/ETATS/ValveEF";

// État de la pompe
static const std::string TOPIC_POMPE = "RAM/PANNEAU/ETATS/Pompe";

// Alarmes de débordement
static const std::string TOPIC_DEBORDEMENT_GROS = "RAM/ALARMES/ETATS/ALR_GB_OVF";
static const std::string TOPIC_DEBORDEMENT_PETIT = "RAM/ALARMES/ETATS/ALR_PB_OVF";

// Courants disponibles dans l'interface Power Meter
static const std::string TOPIC_COURANT[8] = {
    "RAM/POWERMETER/ETATS/Ia",
    "RAM/POWERMETER/ETATS/Ib",
    "", "", "", "", "", ""};

static void envoyerSiTopic(Mqtt &mqtt,
                           const std::string &topic,
                           double valeur)
{
    if (!topic.empty())
    {
        mqtt.envoyer(topic, valeur);
    }
}

static void envoyerLecture(Mqtt &mqtt,
                           const PanelReading &lecture,
                           double consignePetit,
                           double consigneTempPetit)
{
    envoyerSiTopic(mqtt, TOPIC_CONSIGNE_PETIT, consignePetit);
    envoyerSiTopic(mqtt, TOPIC_CONSIGNE_TEMP_PETIT, consigneTempPetit);
    envoyerSiTopic(mqtt, TOPIC_NIVEAU_GROS, lecture.niveauGrosBassin);
    envoyerSiTopic(mqtt, TOPIC_NIVEAU_PETIT, lecture.niveauPetitBassin);
    envoyerSiTopic(mqtt, TOPIC_TEMP_GROS, lecture.temperatureGrosBassin);
    envoyerSiTopic(mqtt, TOPIC_TEMP_PETIT, lecture.temperaturePetitBassin);

    envoyerSiTopic(mqtt, TOPIC_VALVE_HOT, lecture.ouvertureValve[static_cast<int>(Valve::HotWater)]);
    envoyerSiTopic(mqtt, TOPIC_VALVE_LARGE, lecture.ouvertureValve[static_cast<int>(Valve::LargeTank)]);
    envoyerSiTopic(mqtt, TOPIC_VALVE_SMALL, lecture.ouvertureValve[static_cast<int>(Valve::SmallTank)]);
    envoyerSiTopic(mqtt, TOPIC_VALVE_COLD, lecture.ouvertureValve[static_cast<int>(Valve::ColdWater)]);

    for (int i = 0; i < 8; ++i)
    {
        envoyerSiTopic(mqtt, TOPIC_COURANT[i], lecture.courant[i]);
    }

    mqtt.envoyer(TOPIC_DEBORDEMENT_GROS,
                 lecture.debordementGrosBassin ? "ON" : "OFF");

    mqtt.envoyer(TOPIC_DEBORDEMENT_PETIT,
                 lecture.debordementPetitBassin ? "ON" : "OFF");
}

int main()
{
    RunPID::Reglages reglages;
    
    bool pompeEnMarche = false;

    Mqtt mqtt;
    if (!mqtt.connecter("172.17.15.112", 1883))
    {
        printf("Erreur MQTT : connexion impossible\n");
        return 1;
    }

    Panel panneau;
    if (!panneau.open("/dev/pts/2"))//"/tmp/ttyRAM"
    {
        printf("Erreur : %s\n", panneau.lastError());
        mqtt.deconnecter();
        return 1;
    }

    double ouvertureEauChaude = 0.0;
    double ouvertureEauFroide = 0.0;
    double ouvertureGrandManuelle = 0.0;
    double ouverturePetitManuelle = 0.0;

    std::mutex mutexPanneau;
    RunPID runPIDThread(panneau, mutexPanneau);
    RunMqtt reglagesMqtt(panneau, mutexPanneau);
    
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
                envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
            }
            if (debordementTraite && !lecture.debordementGrosBassin && !lecture.debordementPetitBassin)
                runPIDThread.confirmerArretDebordement();
        }
        else
        {
            debordementTraite = false;
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
                envoyerSiTopic(mqtt, TOPIC_POMPE,
                               pompeEnMarche ? 1.0 : 0.0);
            }
            else if (touche == 'o' || touche == 'O')
            {
                passerEnManuel();
                pompeEnMarche = false;
                fermerSorties(true);
                envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
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

        envoyerLecture(mqtt, lecture, reglages.consignePetit, reglages.consigneTemperature);

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

    runPIDThread.stop();
    runPIDThread.join();
    const bool sortiesFermees = fermerSorties(true);
    envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
    panneau.close();
    mqtt.deconnecter();

    std::printf("\033[?25h");
    std::fflush(stdout);
    if (!erreurPanneau.empty())
        std::fprintf(stderr, "Erreur RunPID : %s\n", erreurPanneau.c_str());
    return erreurPanneau.empty() && sortiesFermees ? 0 : 1;
}
