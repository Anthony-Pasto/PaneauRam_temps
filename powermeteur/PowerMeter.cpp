#include "PowerMeter.h"

// Constructeur
PowerMeter::PowerMeter(SerialPort& port)
    : serial(port),
      requeteActuelle(RequeteElkor::VAN),
      requete{},
      reponse{}
{
}

// Destructeur
PowerMeter::~PowerMeter()
{
}

// Construction de la requête Modbus
void PowerMeter::construireRequete(RequeteElkor registre)
{
    // TODO
}

// Envoi de la requête
bool PowerMeter::envoyerRequete()
{
    // TODO

    return false;
}

// Réception de la réponse
bool PowerMeter::recevoirReponse()
{
    // TODO

    return false;
}

// Vérification du CRC et de la réponse
bool PowerMeter::verifierReponse()
{
    // TODO

    return false;
}

// Conversion Big-endian vers float
float PowerMeter::convertirFloat()
{
    // TODO

    return 0.0f;
}

// Modification de la structure partagée
void PowerMeter::modifierPartage(float valeur)
{
    // TODO
}

// Sélection de la prochaine requête
void PowerMeter::prochaineRequete()
{
    // TODO
}

// Mise à jour du PowerMeter
void PowerMeter::update()
{
    // TODO
}

// Lecture des données partagées
PowerMeterReading PowerMeter::getReading()
{
    std::lock_guard<std::mutex> lock(mutex);

    return partage;
}