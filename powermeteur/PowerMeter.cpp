#include "PowerMeter.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr unsigned char lectureRegistres = 0x03;
constexpr unsigned char octetsParMesure = 4;
constexpr int delaiReponseMs = 200;
constexpr int silenceTrameMs = 5; // Pour un port configuré à 9600 bauds.

struct RegistreMesure {
    RequeteElkor registre;
    float PowerMeterReading::* champ;
};

const RegistreMesure mesures[] = {
    {RequeteElkor::VAN, &PowerMeterReading::VAN},
    {RequeteElkor::VBN, &PowerMeterReading::VBN},
    {RequeteElkor::VAB, &PowerMeterReading::VAB},
    {RequeteElkor::IA, &PowerMeterReading::IA},
    {RequeteElkor::IB, &PowerMeterReading::IB},
    {RequeteElkor::KW, &PowerMeterReading::KW},
    {RequeteElkor::FP, &PowerMeterReading::FP},
    {RequeteElkor::KWh, &PowerMeterReading::KWh}
};
}

PowerMeter::PowerMeter(SerialPort& port, bool motsInverses)
    : serial(port), motsInverses(motsInverses)
{
}

bool PowerMeter::lireRegistre(RequeteElkor registre, float& valeur)
{
    const auto adresse = static_cast<std::uint16_t>(registre);
    unsigned char requete[8] = {
        ELKOR_ADDR, lectureRegistres,
        static_cast<unsigned char>(adresse >> 8),
        static_cast<unsigned char>(adresse & 0xff),
        0x00, 0x02, 0x00, 0x00
    };

    // calculerCRC() renvoie déjà les octets dans l'ordre d'envoi.
    const unsigned int crcRequete = calculerCRC(requete, 6);
    requete[6] = static_cast<unsigned char>(crcRequete >> 8);
    requete[7] = static_cast<unsigned char>(crcRequete & 0xff);
    if (!serial.flushInput() || !serial.write(requete, sizeof requete))
        return false;

    // Un octet de marge permet de rejeter une réponse trop longue.
    unsigned char reponse[10];
    const int recus = serial.readUntilSilence(reponse, sizeof reponse, delaiReponseMs, silenceTrameMs);
    if (recus != 9 || reponse[0] != ELKOR_ADDR ||
        reponse[1] != lectureRegistres || reponse[2] != octetsParMesure)
        return false;

    const unsigned int crcReponse = calculerCRC(reponse, 7);
    const unsigned int crcRecu = (static_cast<unsigned int>(reponse[7]) << 8) | reponse[8];
    if (crcRecu != crcReponse)
        return false;

    const int motHaut = motsInverses ? 5 : 3;
    const int motBas = motsInverses ? 3 : 5;
    const std::uint32_t bits =
        (static_cast<std::uint32_t>(reponse[motHaut]) << 24) |
        (static_cast<std::uint32_t>(reponse[motHaut + 1]) << 16) |
        (static_cast<std::uint32_t>(reponse[motBas]) << 8) |
        static_cast<std::uint32_t>(reponse[motBas + 1]);

    static_assert(sizeof(float) == sizeof(bits) && std::numeric_limits<float>::is_iec559,
                  "Le WattsOn envoie des flottants IEEE 754 de 32 bits.");
    std::memcpy(&valeur, &bits, sizeof valeur);
    return std::isfinite(valeur);
}

void PowerMeter::update()
{
    PowerMeterReading lecture;
    for (const auto& mesure : mesures)
    {
        if (!lireRegistre(mesure.registre, lecture.*(mesure.champ)))
        {
            std::lock_guard<std::mutex> verrou(mutex);
            partage.connexion = false;
            return;
        }
    }

    lecture.connexion = true;
    std::lock_guard<std::mutex> verrou(mutex);
    partage = lecture;
}

PowerMeterReading PowerMeter::getReading()
{
    std::lock_guard<std::mutex> verrou(mutex);
    return partage;
}
