// Thread.cpp : le wrapper de pthreads. La politique, la priorité et le cœur se règlent
// à la création; la fin est coopérative (stop() puis join(), jamais d'arrêt brutal).
#include "Thread.h"

#include <cstdio>
#include <cstring>

Thread::Thread(const char* nom, int politique, int priorite, int coeur)
    : politique(politique), priorite(priorite), coeur(coeur) {
    strncpy(this->nom, nom, sizeof(this->nom) - 1);
    this->nom[sizeof(this->nom) - 1] = '\0';
}

Thread::~Thread() {
    stop();
    join();
}

bool Thread::start(bool autoriserRepli) {
    if (demarre) {
        return true;
    }

    pthread_attr_t attributs;
    struct sched_param parametres{};

    enMarche = true;
    int code = pthread_attr_init(&attributs);
    if (code == 0) {
        if (politique != SCHED_OTHER) {
            code = pthread_attr_setinheritsched(&attributs, PTHREAD_EXPLICIT_SCHED);
            if (code == 0)
                code = pthread_attr_setschedpolicy(&attributs, politique);
            parametres.sched_priority = priorite;
            if (code == 0)
                code = pthread_attr_setschedparam(&attributs, &parametres);
        }
        if (code == 0 && coeur >= 0) {
            cpu_set_t ensemble;
            CPU_ZERO(&ensemble);
            CPU_SET(coeur, &ensemble);
            code = pthread_attr_setaffinity_np(&attributs, sizeof(ensemble), &ensemble);
        }
        if (code == 0)
            code = pthread_create(&fil, &attributs, handler, this);
        pthread_attr_destroy(&attributs);
    }

    if (code != 0 && autoriserRepli) {
        fprintf(stderr, "%s : réglages du thread refusés (%s), essai sans priorité ni cœur imposé.\n",
                nom, strerror(code));
        code = pthread_create(&fil, nullptr, handler, this);
    }

    if (code != 0) {
        enMarche = false;
        fprintf(stderr, "%s : démarrage refusé (%s).\n", nom, strerror(code));
        return false;
    }
    demarre = true;
    return true;
}

void Thread::stop() {
    enMarche = false;
}

void Thread::join() {
    if (demarre) {
        pthread_join(fil, NULL);
        demarre = false;
    }
}

void* Thread::handler(void* argument) {
    Thread* tache = (Thread*)argument;

    pthread_setname_np(pthread_self(), tache->nom);   // le nom que ps -L affiche
    tache->task();
    return NULL;
}
