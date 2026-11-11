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
#include <time.h>
#include <errno.h>
#include "ise.h"

static void uso(const char *prog)
{
    fprintf(stderr, "Uso: %s -p <ES|FR|IT> -e <porcentaje 1-100>\n", prog);
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

int main(int argc, char *argv[])
{
    char *sigla = NULL;
    int porcentaje = -1;
    int opcion;

    while ((opcion = getopt(argc, argv, "p:e:")) != -1) {
        switch (opcion) {
        case 'p': sigla = optarg; break;
        case 'e':
            if (aEntero(optarg, &porcentaje) != 0) uso(argv[0]);
            break;
        default:  uso(argv[0]);
        }
    }
    if (sigla == NULL || porcentaje == -1 || optind != argc) uso(argv[0]);

    int idPais = indicePais(sigla);
    if (idPais < 0) {
        fprintf(stderr, "Error: país '%s' no válido (use ES, FR o IT)\n", sigla);
        exit(EXIT_FAILURE);
    }
    if (porcentaje < 1 || porcentaje > 100) {
        fprintf(stderr, "Error: el porcentaje debe estar entre 1 y 100\n");
        exit(EXIT_FAILURE);
    }

    int cantidad[NUM_SECTORES];
    int total = 0;
    for (int s = 0; s < NUM_SECTORES; s++) {

        cantidad[s] = (int)(MAX_ENCUESTAS[idPais][s] * porcentaje / 100.0 + 0.5);
        total += cantidad[s];
    }

    char nombreArchivo[MAX_NOMBRE];
    snprintf(nombreArchivo, sizeof(nombreArchivo), "encuestas_%s.csv", sigla);

    printf("Generando fichero %s para país %s con %d%% de encuestas\n",
           nombreArchivo, sigla, porcentaje);

    FILE *archivo = fopen(nombreArchivo, "w");
    if (archivo == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    fprintf(archivo, "%s\n", CABECERA_CSV);
    int id = 1;
    for (int s = 0; s < NUM_SECTORES; s++) {
        for (int i = 0; i < cantidad[s]; i++) {
            double sentimiento = (rand() % 10001) / 100.0;
            if (fprintf(archivo, "%d,%d,%s,%s,%d,%.2f\n", id++, idPais, sigla,
                        NOMBRE_SECTOR[s], s, sentimiento) < 0) {
                perror("fprintf");
                fclose(archivo);
                exit(EXIT_FAILURE);
            }
        }
    }

    if (fclose(archivo) == EOF) {
        perror("fclose");
        exit(EXIT_FAILURE);
    }

    printf("Máximos por sector:\n");
    for (int s = 0; s < NUM_SECTORES; s++)
        printf("  %-13s: %d/%d encuestas (%d%% del máximo)\n", NOMBRE_SECTOR[s],
               cantidad[s], MAX_ENCUESTAS[idPais][s], porcentaje);
    printf("Total encuestas: %d\n", total);
    printf("Fichero CSV generado\n");

    return EXIT_SUCCESS;
}
