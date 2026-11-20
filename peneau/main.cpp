#include "Panneau.h"
#include "Pid.h"
#include "Mqtt.h"
#include "Keyboard.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

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

static void afficherBarre(const char* titre, double valeur, double consigne)
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

/*static const std::string TOPIC_PID_P = "";
static const std::string TOPIC_PID_I = "";
static const std::string TOPIC_PID_D = "";
static const std::string TOPIC_PID_DEMANDE = "";*/

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
                           const Pid &pid,
                           double consigneGros,
                           double consignePetit,
                           double consigneTempGros,
                           double consigneTempPetit,
                           double ouvertureValveGros)
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

    /*envoyerSiTopic(mqtt, TOPIC_PID_P, pid.termP());
    envoyerSiTopic(mqtt, TOPIC_PID_I, pid.termI());
    envoyerSiTopic(mqtt, TOPIC_PID_D, pid.termD());
    envoyerSiTopic(mqtt, TOPIC_PID_DEMANDE, pid.requested());*/

    (void)ouvertureValveGros;
}

int main()
{
    bool modeAutomatique = false;
    bool pompeEnMarche = false;
    double consigneGros = 50.0;
    double consignePetit = 50.0;
    double consigneTempGros = 30.0;
    double consigneTempPetit = 30.0;
    bool temperaturePetitControlee = false;

    Mqtt mqtt;
    if (!mqtt.connecter("172.17.15.112", 1883))
    {
        printf("Erreur MQTT : connexion impossible\n");
        return 1;
    }

    Panel panneau;
    if (!panneau.open("/tmp/ttyRAM"))
    {
        printf("Erreur : %s\n", panneau.lastError());
        mqtt.deconnecter();
        return 1;
    }

    Pid pidGros(30.0, 12.0, 18.75, 0.0, 100.0);
    Pid pidPetit(15.0, 6.0, 9.43, 0.0, 100.0);
    Pid pidTempPetit(12.0, 4.8, 7.5, -100.0, 100.0);
    pidTempPetit.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    double ouvertureEauChaude = 0.0;
    double ouvertureEauFroide = 0.0;
    double ouvertureGrandManuelle = 0.0;
    double ouverturePetitManuelle = 0.0;
    pidGros.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    pidPetit.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    pidGros.reset();
    pidPetit.reset();

    Keyboard clavier;
    enum class Cible
    {
        NiveauGros,
        NiveauPetit,
        TempGros,
        TempPetit
    };
    Cible cible = Cible::NiveauGros;
    enum class ValveManuelle { Grand, Petit, Chaude, Froide };
    ValveManuelle valveManuelle = ValveManuelle::Grand;
    std::string saisie;

    auto dernierCalcul = std::chrono::steady_clock::now();

    std::printf("\033[?25l");
    effacerEcran();

    while (true)
    {
        if (clavier.kbhit(100))
        {
            char touche = clavier.getch();

            if (touche == 'q' || touche == 'Q')
                break;

            if (!modeAutomatique && touche == '1') {
                valveManuelle = ValveManuelle::Grand;
            } else if (!modeAutomatique && touche == '2') {
                valveManuelle = ValveManuelle::Petit;
            } else if (!modeAutomatique && touche == '3') {
                valveManuelle = ValveManuelle::Chaude;
            } else if (!modeAutomatique && touche == '4') {
                valveManuelle = ValveManuelle::Froide;
            } else if (!modeAutomatique && (touche == '+' || touche == '=')) {
                if (valveManuelle == ValveManuelle::Grand) {
                    ouvertureGrandManuelle = bornerPourcentage(ouvertureGrandManuelle + 10.0);
                    panneau.setValve(Valve::LargeTank, ouvertureGrandManuelle);
                } else if (valveManuelle == ValveManuelle::Petit) {
                    ouverturePetitManuelle = bornerPourcentage(ouverturePetitManuelle + 10.0);
                    panneau.setValve(Valve::SmallTank, ouverturePetitManuelle);
                } else if (valveManuelle == ValveManuelle::Chaude) {
                    ouvertureEauChaude = bornerPourcentage(ouvertureEauChaude + 10.0);
                    panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                } else {
                    ouvertureEauFroide = bornerPourcentage(ouvertureEauFroide + 10.0);
                    panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                }
            } else if (!modeAutomatique && touche == '-') {
                if (valveManuelle == ValveManuelle::Grand) {
                    ouvertureGrandManuelle = bornerPourcentage(ouvertureGrandManuelle - 10.0);
                    panneau.setValve(Valve::LargeTank, ouvertureGrandManuelle);
                } else if (valveManuelle == ValveManuelle::Petit) {
                    ouverturePetitManuelle = bornerPourcentage(ouverturePetitManuelle - 10.0);
                    panneau.setValve(Valve::SmallTank, ouverturePetitManuelle);
                } else if (valveManuelle == ValveManuelle::Chaude) {
                    ouvertureEauChaude = bornerPourcentage(ouvertureEauChaude - 10.0);
                    panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                } else {
                    ouvertureEauFroide = bornerPourcentage(ouvertureEauFroide - 10.0);
                    panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                }
            } else if (touche == 'g' || touche == 'G')
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
                temperaturePetitControlee = !temperaturePetitControlee;
                pidTempPetit.reset();
                pidTempPetit.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
                dernierCalcul = std::chrono::steady_clock::now();
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
                        consigneGros = valeur;
                        break;
                    case Cible::NiveauPetit:
                        consignePetit = valeur;
                        break;
                    case Cible::TempGros:
                        consigneTempGros = valeur;
                        break;
                    case Cible::TempPetit:
                        consigneTempPetit = valeur;
                        break;
                    }

                    saisie.clear();
                }
            }
            else if (touche == 'a' || touche == 'A')
            {
                modeAutomatique = true;
                pidGros.reset();
                pidPetit.reset();
                pidTempPetit.reset();
                dernierCalcul = std::chrono::steady_clock::now();
            }
            else if (touche == 'm' || touche == 'M')
            {
                modeAutomatique = false;
                panneau.setValve(Valve::HotWater, 0.0);
                panneau.setValve(Valve::ColdWater, 0.0);
                panneau.setHotWater(false);
                panneau.setColdWater(false);
                ouvertureEauChaude = 0.0;
                ouvertureEauFroide = 0.0;
                ouvertureGrandManuelle = 0.0;
                ouverturePetitManuelle = 0.0;
                panneau.setValve(Valve::LargeTank, 0.0);
                panneau.setValve(Valve::SmallTank, 0.0);
            }
            else if (touche == 'p' || touche == 'P')
            {
                pompeEnMarche = !pompeEnMarche;
                panneau.setPump(pompeEnMarche);
                envoyerSiTopic(mqtt, TOPIC_POMPE,
                               pompeEnMarche ? 1.0 : 0.0);
            }
            else if (touche == 'o' || touche == 'O')
            {
                modeAutomatique = false;
                pompeEnMarche = false;
                panneau.setPump(false);
                panneau.setValve(Valve::LargeTank, 0.0);
                panneau.setValve(Valve::SmallTank, 0.0);
                panneau.setValve(Valve::HotWater, 0.0);
                panneau.setValve(Valve::ColdWater, 0.0);
                ouvertureEauChaude = 0.0;
                ouvertureEauFroide = 0.0;
                envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
            }
            else if (touche == 'l' || touche == 'L')
            {
                // Ouverture manuelle des valves
                modeAutomatique = false;
                double ouverture = 0.0;
                printf("Ouverture manuelle (0-100) : ");
                if (scanf("%lf", &ouverture) == 1)
                {
                    ouverture = bornerPourcentage(ouverture);
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
                modeAutomatique = false;
                double ouverture = 0.0;
                printf("Ouverture manuelle (0-100) : ");
                if (scanf("%lf", &ouverture) == 1)
                {
                    ouverture = bornerPourcentage(ouverture);
                    panneau.setValve(Valve::SmallTank, ouverture);
                    ouverturePetitManuelle = ouverture;
                }
                printf("Appuyez sur Entrée pour continuer...");
                getchar(); // Pour consommer le '\n' restant
                getchar();
            }
            else if (!modeAutomatique && (touche == 'h' || touche == 'H')) {
                valveManuelle = ValveManuelle::Chaude;
                ouvertureEauChaude = (ouvertureEauChaude > 0.0) ? 0.0 : 100.0;
                panneau.setValve(Valve::HotWater, ouvertureEauChaude);
                panneau.setHotWater(ouvertureEauChaude > 0.0);
            } else if (!modeAutomatique && (touche == 'f' || touche == 'F')) {
                valveManuelle = ValveManuelle::Froide;
                ouvertureEauFroide = (ouvertureEauFroide > 0.0) ? 0.0 : 100.0;
                panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
                panneau.setColdWater(ouvertureEauFroide > 0.0);
            }
        }

        PanelReading lecture;
   
        if (!panneau.read(lecture))
        {
            effacerEcran();
            std::printf("Erreur panneau : %s\n", panneau.lastError());
            panneau.setValve(Valve::LargeTank, 0.0);
            panneau.setValve(Valve::SmallTank, 0.0);
            panneau.setPump(false);
            pompeEnMarche = false;
            envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);

            std::printf("Erreur panneau : %s\n", panneau.lastError());
            break;
        }

        if (lecture.debordementGrosBassin || lecture.debordementPetitBassin)
        {
            modeAutomatique = false;
            pompeEnMarche = false;
            panneau.setPump(false);
            panneau.setValve(Valve::LargeTank, 0.0);
            panneau.setValve(Valve::SmallTank, 0.0);
            panneau.setValve(Valve::HotWater, 0.0);
            panneau.setValve(Valve::ColdWater, 0.0);
            ouvertureEauChaude = 0.0;
            ouvertureEauFroide = 0.0;
            envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
        }

        double ouvertureGros = 0.0;
        double ouverturePetit = 0.0;

        if (modeAutomatique)
        {
            auto maintenant = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(maintenant - dernierCalcul).count();
            if (dt <= 0.0)
                dt = 0.001;
            if (dt > 2.0)
                dt = 2.0;
            dernierCalcul = maintenant;

            ouvertureGros = pidGros.compute(consigneGros,
                                            lecture.niveauGrosBassin, dt);

            // Le PID de niveau reste toujours actif dans les deux modes.
            ouverturePetit = pidPetit.compute(consignePetit,
                                              lecture.niveauPetitBassin, dt);

            if (temperaturePetitControlee)
            {
                double commandeTemperature = pidTempPetit.compute(
                    consigneTempPetit,
                    lecture.temperaturePetitBassin,
                    dt
                );

                // Les deux valves proportionnelles peuvent être ouvertes ensemble.
                ouvertureEauChaude = bornerPourcentage(50.0 + commandeTemperature / 2.0);
                ouvertureEauFroide = bornerPourcentage(50.0 - commandeTemperature / 2.0);
            }
            else
            {
                ouvertureEauChaude = 0.0;
                ouvertureEauFroide = 0.0;
            }

            panneau.setValve(Valve::LargeTank, ouvertureGros);
            panneau.setValve(Valve::SmallTank, ouverturePetit);
            panneau.setValve(Valve::HotWater, ouvertureEauChaude);
            panneau.setValve(Valve::ColdWater, ouvertureEauFroide);

            panneau.setHotWater(ouvertureEauChaude > 0.0);
            panneau.setColdWater(ouvertureEauFroide > 0.0);
        }

        envoyerLecture(mqtt, lecture, pidGros,
                       consigneGros, consignePetit,
                       consigneTempGros, consigneTempPetit, ouvertureGros);

        effacerEcran();
        //fait par ai 
        std::printf("\033[1;36m+============================================================+\033[0m\n");
        std::printf("\033[1;36m|                 PANNEAU RAM - CONTROLE                     |\033[0m\n");
        std::printf("\033[1;36m+============================================================+\033[0m\n");

        std::printf(" MODE : \033[1;%sm%-12s\033[0m",
                    modeAutomatique ? "32" : "33",
                    modeAutomatique ? "AUTOMATIQUE" : "MANUEL");
        std::printf(" POMPE : \033[1;%sm%-3s\033[0m\n",
                    pompeEnMarche ? "32" : "31",
                    pompeEnMarche ? "ON" : "OFF");

        std::printf(" TEMPERATURE : \033[1;%sm%-3s\033[0m",
                    temperaturePetitControlee ? "32" : "31",
                    temperaturePetitControlee ? "ON" : "OFF");
        std::printf(" DEBORDEMENT : ");
        if (lecture.debordementGrosBassin || lecture.debordementPetitBassin)
            std::printf("\033[1;31mALERTE\033[0m\n");
        else
            std::printf("\033[1;32mNORMAL\033[0m\n");

        std::printf("\n--- NIVEAUX ET CONSIGNES ----------------------------------\n");
        afficherBarre("Gros bassin", lecture.niveauGrosBassin, consigneGros);
        afficherBarre("Petit bassin", lecture.niveauPetitBassin, consignePetit);
        std::printf("     Grand valve = %3.0f %%   |   Petit valve = %3.0f %%\n",
                    ouvertureGros, ouverturePetit);

        std::printf("\n--- TEMPERATURE PETIT BASSIN -----------------------------\n");
        std::printf("  Actuelle : %3.0f C   Cible : %3.0f C\n",
                    lecture.temperaturePetitBassin, consigneTempPetit);
        std::printf("  Eau chaude : %3.0f %%   Eau froide : %3.0f %%\n",
                    ouvertureEauChaude, ouvertureEauFroide);

        std::printf("\n--- COMMANDE ------------------------------------------------\n");
        const char* nomCible = "NIV. GROS";
        if (cible == Cible::NiveauPetit) nomCible = "NIV. PETIT";
        if (cible == Cible::TempGros) nomCible = "TEMP GROS";
        if (cible == Cible::TempPetit) nomCible = "TEMP PETIT";
        std::printf("  Cible : %-12s Valeur saisie : %s\n",
                    nomCible,
                    saisie.empty() ? "---" : saisie.c_str());

        const char* nomValve = "GROS";
        if (valveManuelle == ValveManuelle::Petit) nomValve = "PETIT";
        if (valveManuelle == ValveManuelle::Chaude) nomValve = "EAU CHAUDE";
        if (valveManuelle == ValveManuelle::Froide) nomValve = "EAU FROIDE";

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

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    panneau.setValve(Valve::LargeTank, 0.0);
    panneau.setValve(Valve::SmallTank, 0.0);
    panneau.setPump(false);
    panneau.setColdWater(false); // Fermer eau froide
    panneau.setHotWater(false);  // Fermer eau chaude
    envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
    panneau.close();
    mqtt.deconnecter();
    //effacerEcran();
   // std::printf("\033[?25h");
   
    std::fflush(stdout);
    return 0;
}