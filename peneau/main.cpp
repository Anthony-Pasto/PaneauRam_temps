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
    Pid pidTempPetit(2.0, 0.1, 1.0, -100.0, 100.0);
    pidTempPetit.setAntiWindup(Pid::AntiWindup::ConditionalIntegration);
    double ouvertureEauChaude = 0.0;
    double ouvertureEauFroide = 0.0;
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

            if (touche == 'g' || touche == 'G')
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
                if (temperaturePetitControlee)
                {
                    temperaturePetitControlee = false;
                }
                else
                {
                    temperaturePetitControlee = true;
                }
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
                ouvertureEauChaude = 0.0;
                ouvertureEauFroide = 0.0;
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
            dernierCalcul = maintenant;

            ouvertureGros = pidGros.compute(consigneGros,
                                            lecture.niveauGrosBassin, dt);
            if (temperaturePetitControlee)
            {

                ouverturePetit = pidPetit.compute(consignePetit,
                                                  lecture.niveauPetitBassin, dt);
            }
            else
            {
                // Mélange proportionnel : les deux valves peuvent rester ouvertes ensemble.
                // 50 % chacune au point neutre; la commande déplace l'équilibre.
                ouvertureEauChaude = bornerPourcentage(50.0 + commandeTemperature / 2.0);
                ouvertureEauFroide = bornerPourcentage(50.0 - commandeTemperature / 2.0);
            }

            panneau.setValve(Valve::LargeTank, ouvertureGros);

            if (temperaturePetitControlee)
            panneau.setValve(Valve::SmallTank, ouverturePetit);
            else
            {
                panneau.setValve(Valve::SmallTank, 0.0);
                panneau.setValve(Valve::HotWater, ouvertureEauChaude);
            panneau.setValve(Valve::ColdWater, ouvertureEauFroide);
            }
             

            double commandeTemperature = pidTempPetit.compute(
                consigneTempPetit,
                lecture.temperaturePetitBassin,
                dt);

           
        }

        envoyerLecture(mqtt, lecture, pidGros,
                       consigneGros, consignePetit,
                       consigneTempGros, consigneTempPetit, ouvertureGros);

        effacerEcran();
        std::printf("\033[H\033[2J");
        std::printf("=============== PANNEAU RAM ===============\n");
        std::printf("Mode : %-10s   Pompe : %s\n",
                    modeAutomatique ? "AUTOMATIQUE" : "MANUEL",
                    pompeEnMarche ? "ON" : "OFF");
        const char *nomCible = "NIV. GROS";
        if (cible == Cible::NiveauPetit)
            nomCible = "NIV. PETIT";
        if (cible == Cible::TempGros)
            nomCible = "TEMP GROS";
        if (cible == Cible::TempPetit)
            nomCible = "TEMP PETIT";

        std::printf("Cible saisie : %-10s Valeur saisie : %s\n",
                    nomCible,
                    saisie.empty() ? "---" : saisie.c_str());
        std::printf("------------------------------------------\n");
        std::printf("Grand bassin : niveau %3.0f %% | consigne %3.0f %% | valve %3.0f %%\n",
                    lecture.niveauGrosBassin, consigneGros, ouvertureGros);
        std::printf("Petit bassin : niveau %3.0f %% | consigne %3.0f %% | valve %3.0f %%\n",
                    lecture.niveauPetitBassin, consignePetit, ouverturePetit);
        std::printf("Temperature petit : %3.0f C | consigne %3.0f C | Chaude=%3.0f %% Froid=%3.0f %%\n",
                    lecture.temperaturePetitBassin,
                    consigneTempPetit,
                    ouvertureEauChaude,
                    ouvertureEauFroide);
        std::printf("Debordement : grand=%s petit=%s\n",
                    lecture.debordementGrosBassin ? "OUI" : "NON",
                    lecture.debordementPetitBassin ? "OUI" : "NON");
        std::printf("------------------------------------------\n");
        std::printf("G/S : niveau bassin | T/U : temperature bassin | 0-100 : valeur | Entree : appliquer\n");
        std::printf("A : auto | M : manuel | P : pompe | O : arret | Q : quitter\n");
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    panneau.setValve(Valve::LargeTank, 0.0);
    panneau.setValve(Valve::SmallTank, 0.0);
    panneau.setPump(false);
    envoyerSiTopic(mqtt, TOPIC_POMPE, 0.0);
    panneau.close();
    mqtt.deconnecter();
    effacerEcran();
    std::printf("\033[?25h");
    std::fflush(stdout);
    return 0;
}