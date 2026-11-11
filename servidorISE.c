/****************************************************************
* Pontificia Universidad Javeriana                              *
* Autor: Marcelo Molina                                         *
* Materia: Sistemas Operativos                                  *
* Temas: Proyecto                                               *
*                                                               *
*                                                               *
****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "ise.h"

#define ESPERA_POLL_MS 250
#define ID_FIN        (-1)

typedef struct {
    int       activo;
    int       completo;
    pid_t     pid;
    char      pais[3];
    int       totalEsperado;
    int       recibidas;

    Encuesta *buffer;
    int       entrada, salida;
    sem_t     vacios, llenos, mutex;
    pthread_t hilo;

    double    suma[NUM_SECTORES];
    int       cuenta[NUM_SECTORES];
} Agente;

static Agente agentes[MAX_AGENTES];
static int    maxAgentes;
static int    tamBuffer;
static int    tiempoServidor;
static int    fdPipe = -1;

static int             tiempoAgotado = 0;
static pthread_mutex_t mutexTiempo   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutexPantalla = PTHREAD_MUTEX_INITIALIZER;

static void uso(const char *prog)
{
    fprintf(stderr, "Uso: %s -p <PipeN> -t <time %d-%d> -a <agentes 1-%d> "
            "-b <tamB>\n", prog, TIEMPO_MIN, TIEMPO_MAX, MAX_AGENTES);
    exit(EXIT_FAILURE);
}

static int aEntero(const char *texto, int *valor)
{
    char *fin;
    errno = 0;
    long v = strtol(texto, &fin, 10);
    if (errno != 0 || *fin != '\0' || fin == texto) return -1;
    *valor = (int)v;
    return 0;
}

static int tiempoTerminado(void)
{
    pthread_mutex_lock(&mutexTiempo);
    int t = tiempoAgotado;
    pthread_mutex_unlock(&mutexTiempo);
    return t;
}

static double calcularISEDatos(const double suma[], const int cuenta[],
                               double promedio[])
{
    double ise = 0.0, pesoUsado = 0.0;
    for (int s = 0; s < NUM_SECTORES; s++) {
        promedio[s] = cuenta[s] > 0 ? suma[s] / cuenta[s] : 0.0;
        if (cuenta[s] > 0) {
            ise += PESO_SECTOR[s] * promedio[s];
            pesoUsado += PESO_SECTOR[s];
        }
    }
    return pesoUsado > 0 ? ise / pesoUsado : 0.0;
}

static void imprimirTablaSectores(const double promedio[], const int cuenta[])
{
    printf("  %-13s %8s %10s %6s\n", "Sector", "Encuest.", "Promedio", "Peso");
    for (int s = 0; s < NUM_SECTORES; s++)
        printf("  %-13s %8d %9.2f%% %5.0f%%\n", NOMBRE_SECTOR[s], cuenta[s],
               promedio[s], PESO_SECTOR[s] * 100);
}

static void insertarEnBuffer(Agente *ag, const Encuesta *e)
{
    sem_wait(&ag->vacios);
    sem_wait(&ag->mutex);
    ag->buffer[ag->entrada] = *e;
    ag->entrada = (ag->entrada + 1) % tamBuffer;
    sem_post(&ag->mutex);
    sem_post(&ag->llenos);
}

static Encuesta sacarDeBuffer(Agente *ag)
{
    Encuesta e;
    sem_wait(&ag->llenos);
    sem_wait(&ag->mutex);
    e = ag->buffer[ag->salida];
    ag->salida = (ag->salida + 1) % tamBuffer;
    sem_post(&ag->mutex);
    sem_post(&ag->vacios);
    return e;
}

static void insertarFin(Agente *ag)
{
    Encuesta fin;
    memset(&fin, 0, sizeof(fin));
    fin.idEncuesta = ID_FIN;
    insertarEnBuffer(ag, &fin);
}

static void *hiloAgente(void *arg)
{
    Agente *ag = (Agente *)arg;

    for (;;) {
        Encuesta e = sacarDeBuffer(ag);
        if (e.idEncuesta == ID_FIN) break;
        ag->suma[e.idSector] += e.sentimiento;
        ag->cuenta[e.idSector]++;
        ag->recibidas++;
    }

    double promedio[NUM_SECTORES];
    double ise = calcularISEDatos(ag->suma, ag->cuenta, promedio);

    pthread_mutex_lock(&mutexPantalla);
    printf("\n----------- REPORTE PARCIAL - %s (pid %d) -----------\n",
           ag->pais, (int)ag->pid);
    printf("  Estado: %s  (%d/%d encuestas)\n",
           ag->completo ? "lectura completa" : "interrumpido por tiempo",
           ag->recibidas, ag->totalEsperado);
    imprimirTablaSectores(promedio, ag->cuenta);
    printf("  ISE %s = %.2f%%  ->  %s\n", ag->pais, ise, recomendacion(ise));
    printf("-----------------------------------------------------\n");
    fflush(stdout);
    pthread_mutex_unlock(&mutexPantalla);

    return NULL;
}

static void *hiloTemporizador(void *arg)
{
    (void)arg;
    sleep((unsigned)tiempoServidor);

    pthread_mutex_lock(&mutexTiempo);
    tiempoAgotado = 1;
    pthread_mutex_unlock(&mutexTiempo);

    pthread_mutex_lock(&mutexPantalla);
    printf("\n>>> Tiempo de %d s agotado. Cerrando recepción...\n",
           tiempoServidor);
    fflush(stdout);
    pthread_mutex_unlock(&mutexPantalla);
    return NULL;
}

static Agente *buscarAgente(pid_t pid)
{
    for (int i = 0; i < maxAgentes; i++)
        if (agentes[i].activo && agentes[i].pid == pid) return &agentes[i];
    return NULL;
}

static int registrarAgente(const Mensaje *msg)
{
    Agente *ag = NULL;
    for (int i = 0; i < maxAgentes; i++)
        if (!agentes[i].activo) { ag = &agentes[i]; break; }

    if (ag == NULL) {
        pthread_mutex_lock(&mutexPantalla);
        printf("[ISE] Rechazado agente %s (pid %d): máximo de %d agentes.\n",
               msg->encuesta.pais, (int)msg->pid, maxAgentes);
        pthread_mutex_unlock(&mutexPantalla);
        return -1;
    }

    memset(ag, 0, sizeof(*ag));
    ag->pid = msg->pid;
    ag->totalEsperado = msg->totalEncuestas;
    strncpy(ag->pais, msg->encuesta.pais, sizeof(ag->pais) - 1);

    ag->buffer = calloc((size_t)tamBuffer, sizeof(Encuesta));
    if (ag->buffer == NULL) { perror("calloc"); exit(EXIT_FAILURE); }

    if (sem_init(&ag->vacios, 0, (unsigned)tamBuffer) == -1 ||
        sem_init(&ag->llenos, 0, 0) == -1 ||
        sem_init(&ag->mutex,  0, 1) == -1) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }

    ag->activo = 1;
    int err = pthread_create(&ag->hilo, NULL, hiloAgente, ag);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&mutexPantalla);
    printf("[ISE] Conectado agente %s (pid %d) - %d encuestas por recibir.\n",
           ag->pais, (int)ag->pid, ag->totalEsperado);
    pthread_mutex_unlock(&mutexPantalla);
    return 0;
}

static int leerMensaje(Mensaje *msg)
{
    size_t leidos = 0;
    while (leidos < sizeof(Mensaje)) {
        ssize_t n = read(fdPipe, (char *)msg + leidos, sizeof(Mensaje) - leidos);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("read");
            exit(EXIT_FAILURE);
        }
        if (n == 0) return 0;
        leidos += (size_t)n;
    }
    return 1;
}

static void *hiloRecolector(void *arg)
{
    (void)arg;
    struct pollfd pfd = { .fd = fdPipe, .events = POLLIN };
    Mensaje msg;

    while (!tiempoTerminado()) {

        int listo = poll(&pfd, 1, ESPERA_POLL_MS);
        if (listo == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            exit(EXIT_FAILURE);
        }
        if (listo == 0 || !(pfd.revents & POLLIN)) continue;
        if (!leerMensaje(&msg)) continue;

        Agente *ag;
        switch (msg.tipo) {
        case MSG_CONEXION:
            registrarAgente(&msg);
            break;
        case MSG_ENCUESTA:
            ag = buscarAgente(msg.pid);
            if (ag != NULL && !ag->completo) insertarEnBuffer(ag, &msg.encuesta);
            break;
        case MSG_FIN:
            ag = buscarAgente(msg.pid);
            if (ag != NULL && !ag->completo) {
                ag->completo = 1;
                insertarFin(ag);
            }
            break;
        default:
            fprintf(stderr, "[ISE] Mensaje desconocido (tipo %d)\n", msg.tipo);
        }
    }
    return NULL;
}

static void reporteFinal(void)
{
    double sumaTotal[NUM_SECTORES] = {0};
    int    cuentaTotal[NUM_SECTORES] = {0};
    double promedio[NUM_SECTORES];
    int    conectados = 0;

    printf("\n╔═══════════════════════════════════════════════════╗\n");
    printf("║   RESUMEN GENERAL - INDICADOR DE SENTIMIENTO ISE  ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");
    printf("Tiempo de servicio: %d s | Buffer: %d | Agentes máx: %d\n\n",
           tiempoServidor, tamBuffer, maxAgentes);

    printf("  %-5s %10s %10s %10s\n", "País", "Encuestas", "ISE", "Recomend.");
    for (int i = 0; i < maxAgentes; i++) {
        Agente *ag = &agentes[i];
        if (!ag->activo) continue;
        conectados++;
        double ise = calcularISEDatos(ag->suma, ag->cuenta, promedio);
        printf("  %-5s %10d %9.2f%% %10s\n", ag->pais, ag->recibidas, ise,
               recomendacion(ise));
        for (int s = 0; s < NUM_SECTORES; s++) {
            sumaTotal[s]   += ag->suma[s];
            cuentaTotal[s] += ag->cuenta[s];
        }
    }

    if (conectados == 0) {
        printf("\n  No se conectó ningún agente: no hay datos para el ISE.\n");
        return;
    }

    double ise = calcularISEDatos(sumaTotal, cuentaTotal, promedio);
    printf("\n  Consolidado por sector:\n");
    imprimirTablaSectores(promedio, cuentaTotal);
    printf("\n  ISE GENERAL = %.2f%%\n", ise);
    printf("  RECOMENDACIÓN: %s\n", recomendacion(ise));
    printf("  (Vender: > 50%% | Comprar: < 50%% | Neutro: = 50%%)\n");
}

static int procesoHijo(const char *nombrePipe)
{

    fdPipe = open(nombrePipe, O_RDWR);
    if (fdPipe == -1) { perror("open pipe"); return EXIT_FAILURE; }

    printf("[ISE] Servidor listo en pipe '%s' durante %d s. Esperando agentes...\n",
           nombrePipe, tiempoServidor);
    fflush(stdout);

    pthread_t temporizador, recolector;
    int err;
    if ((err = pthread_create(&temporizador, NULL, hiloTemporizador, NULL)) != 0 ||
        (err = pthread_create(&recolector, NULL, hiloRecolector, NULL)) != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        return EXIT_FAILURE;
    }

    pthread_join(recolector, NULL);
    pthread_join(temporizador, NULL);

    for (int i = 0; i < maxAgentes; i++)
        if (agentes[i].activo && !agentes[i].completo) insertarFin(&agentes[i]);

    for (int i = 0; i < maxAgentes; i++)
        if (agentes[i].activo) pthread_join(agentes[i].hilo, NULL);

    reporteFinal();

    for (int i = 0; i < maxAgentes; i++) {
        if (!agentes[i].activo) continue;
        sem_destroy(&agentes[i].vacios);
        sem_destroy(&agentes[i].llenos);
        sem_destroy(&agentes[i].mutex);
        free(agentes[i].buffer);
    }
    if (close(fdPipe) == -1) perror("close");
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    char *nombrePipe = NULL;
    int opcion;
    maxAgentes = tamBuffer = tiempoServidor = -1;

    while ((opcion = getopt(argc, argv, "b:p:t:a:")) != -1) {
        switch (opcion) {
        case 'b': if (aEntero(optarg, &tamBuffer) != 0)      uso(argv[0]); break;
        case 't': if (aEntero(optarg, &tiempoServidor) != 0) uso(argv[0]); break;
        case 'a': if (aEntero(optarg, &maxAgentes) != 0)     uso(argv[0]); break;
        case 'p': nombrePipe = optarg; break;
        default:  uso(argv[0]);
        }
    }
    if (nombrePipe == NULL || tamBuffer == -1 || tiempoServidor == -1 ||
        maxAgentes == -1 || optind != argc)
        uso(argv[0]);

    if (tiempoServidor < TIEMPO_MIN || tiempoServidor > TIEMPO_MAX) {
        fprintf(stderr, "Error: -t debe estar entre %d y %d segundos\n",
                TIEMPO_MIN, TIEMPO_MAX);
        exit(EXIT_FAILURE);
    }
    if (maxAgentes < 1 || maxAgentes > MAX_AGENTES) {
        fprintf(stderr, "Error: -a debe estar entre 1 y %d\n", MAX_AGENTES);
        exit(EXIT_FAILURE);
    }
    if (tamBuffer < 1) {
        fprintf(stderr, "Error: -b debe ser mayor que 0\n");
        exit(EXIT_FAILURE);
    }

    if (mkfifo(nombrePipe, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }

    fflush(stdout);
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        unlink(nombrePipe);
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
        exit(procesoHijo(nombrePipe));

    int estado;
    if (waitpid(pid, &estado, 0) == -1) {
        perror("waitpid");
        unlink(nombrePipe);
        exit(EXIT_FAILURE);
    }

    if (unlink(nombrePipe) == -1) perror("unlink");

    if (WIFEXITED(estado) && WEXITSTATUS(estado) == EXIT_SUCCESS) {
        printf("\n[ISE] Proceso de recepción finalizó correctamente.\n");
        return EXIT_SUCCESS;
    }
    printf("\n[ISE] El proceso de recepción terminó con errores.\n");
    return EXIT_FAILURE;
}
