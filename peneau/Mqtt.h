#pragma once

#include <mosquitto.h>
#include <mutex>
#include <queue>
#include <string>

class Mqtt {
public:
    struct Message {
        std::string topic;
        std::string contenu;
    };

    Mqtt();
    ~Mqtt();

    bool connecter(const char* ip, int port = 1883);

    bool envoyer(const std::string& topic,
                 const std::string& message);

    bool envoyer(const std::string& topic,
                 double valeur);
    bool abonner(const std::string& topic);
    bool recevoir(Message& message);
    void deconnecter();

    Mqtt(const Mqtt&) = delete;
    Mqtt& operator=(const Mqtt&) = delete;

private:
    static void onMessage(struct mosquitto*, void* contexte,
                          const struct mosquitto_message* message);

    struct mosquitto* client = nullptr;
    bool actif = false;
    bool initialise = false;
    std::mutex mutexMessages;
    std::queue<Message> messagesRecus;
};
