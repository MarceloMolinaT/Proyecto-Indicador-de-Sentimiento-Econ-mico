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
#include <signal.h>
#include "ise.h"

static void uso(const char *prog)
{
    fprintf(stderr, "Uso: %s -n <encuestas_XX.csv> -p <pipeNominal>\n", prog);
    exit(EXIT_FAILURE);
}

static Encuesta *leerEncuestas(const char *nombreArchivo, int *total)
{
    FILE *archivo = fopen(nombreArchivo, "r");
    if (archivo == NULL) {
        perror(nombreArchivo);
        exit(EXIT_FAILURE);
    }

    char linea[MAX_LINEA];

    int lineas = 0;
    while (fgets(linea, sizeof(linea), archivo) != NULL) lineas++;
    *total = lineas - 1;
    if (*total <= 0) {
        fprintf(stderr, "Error: el fichero %s no tiene encuestas\n", nombreArchivo);
        fclose(archivo);
        exit(EXIT_FAILURE);
    }

    Encuesta *encuestas = malloc((size_t)(*total) * sizeof(Encuesta));
    if (encuestas == NULL) {
        perror("malloc");
        fclose(archivo);
        exit(EXIT_FAILURE);
    }

    rewind(archivo);
    if (fgets(linea, sizeof(linea), archivo) == NULL) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }
    int i = 0;
    while (i < *total && fgets(linea, sizeof(linea), archivo) != NULL) {
        Encuesta *e = &encuestas[i];

        if (sscanf(linea, "%d,%d,%2[^,],%*[^,],%d,%lf", &e->idEncuesta,
                   &e->idPais, e->pais, &e->idSector, &e->sentimiento) != 5 ||
            e->idSector < 0 || e->idSector >= NUM_SECTORES) {
            fprintf(stderr, "Advertencia: línea %d inválida, se omite\n", i + 2);
            (*total)--;
            continue;
        }
        i++;
    }

    fclose(archivo);
    return encuestas;
}

static int enviarMensaje(int fd, const Mensaje *msg)
{
    ssize_t n;
    do {
        n = write(fd, msg, sizeof(Mensaje));
    } while (n == -1 && errno == EINTR);

    if (n == -1) {
        if (errno == EPIPE)
            fprintf(stderr, "\nEl servidor ISE cerró la conexión (tiempo agotado).\n");
        else
            perror("write");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char *nombreArchivo = NULL;
    char *nombrePipe = NULL;
    int opcion;

    while ((opcion = getopt(argc, argv, "n:p:")) != -1) {
        switch (opcion) {
        case 'n': nombreArchivo = optarg; break;
        case 'p': nombrePipe = optarg;    break;
        default:  uso(argv[0]);
        }
    }
    if (nombreArchivo == NULL || nombrePipe == NULL || optind != argc)
        uso(argv[0]);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    int total;
    Encuesta *encuestas = leerEncuestas(nombreArchivo, &total);
    const char *pais = encuestas[0].pais;

    printf("╔═══════════════════════════════════════════════╗\n");
    printf("║         AGENTE - ENVÍO DE ENCUESTAS           ║\n");
    printf("╚═══════════════════════════════════════════════╝\n");
    printf("País: %s\n", pais);
    printf("Pipe: %s\n", nombrePipe);
    printf("Leídas %d encuestas.\n", total);
    printf("Conectando al proceso principal...\n");

    int fd = open(nombrePipe, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        if (errno == ENOENT || errno == ENXIO)
            fprintf(stderr, "Error: el servidor ISE no está activo en '%s'\n",
                    nombrePipe);
        else
            perror("open");
        free(encuestas);
        exit(EXIT_FAILURE);
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        perror("fcntl");
        close(fd);
        free(encuestas);
        exit(EXIT_FAILURE);
    }
    printf("Conectado.\n");

    Mensaje msg;
    memset(&msg, 0, sizeof(msg));
    msg.pid = getpid();

    msg.tipo = MSG_CONEXION;
    msg.totalEncuestas = total;
    msg.encuesta = encuestas[0];
    int estado = enviarMensaje(fd, &msg);

    printf("Enviando...\n");
    int enviadas = 0;
    msg.tipo = MSG_ENCUESTA;
    for (int i = 0; i < total && estado == 0; i++) {
        msg.encuesta = encuestas[i];
        estado = enviarMensaje(fd, &msg);
        if (estado == 0) {
            enviadas++;
            if (enviadas % LOTE_PROGRESO == 0)
                printf("%d/%d\n", enviadas, total);
        }
    }

    if (estado == 0) {
        printf("Enviadas %d/%d encuestas.\n", enviadas, total);
        msg.tipo = MSG_FIN;
        estado = enviarMensaje(fd, &msg);
        if (estado == 0) printf("Mensaje de finalización enviado.\n");
    }

    if (close(fd) == -1) perror("close");

    if (estado == 0)
        printf("Envío %s finalizado satisfactoriamente!!.\n", pais);
    else
        printf("Envío %s interrumpido: %d/%d encuestas enviadas.\n",
               pais, enviadas, total);

    free(encuestas);
    return estado == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
