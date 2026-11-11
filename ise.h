/****************************************************************
* Pontificia Universidad Javeriana                              *
* Autor: Marcelo Molina                                         *
* Materia: Sistemas Operativos                                  *
* Temas: Proyecto                                               *
*                                                               *
*                                                               *
****************************************************************/

#ifndef ISE_H
#define ISE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>

#define NUM_PAISES      3
#define NUM_SECTORES    5

#define MAX_AGENTES     4
#define TIEMPO_MIN      30
#define TIEMPO_MAX      120

#define LOTE_PROGRESO   500
#define MAX_LINEA       256
#define MAX_NOMBRE      64

#define UMBRAL_ISE      50.0

#define CABECERA_CSV "ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento"

typedef enum { PAIS_ES = 0, PAIS_FR = 1, PAIS_IT = 2 } Pais;

typedef enum {
    SEC_INDUSTRIA    = 0,
    SEC_SERVICIOS    = 1,
    SEC_CONSUMIDORES = 2,
    SEC_COMERCIO     = 3,
    SEC_CONSTRUCCION = 4
} Sector;

static const char *const SIGLA_PAIS[NUM_PAISES] = { "ES", "FR", "IT" };

static const char *const NOMBRE_SECTOR[NUM_SECTORES] = {
    "Industria", "Servicios", "Consumidores", "Comercio", "Construccion"
};

static const double PESO_SECTOR[NUM_SECTORES] = {
    0.40,
    0.30,
    0.20,
    0.05,
    0.05
};

static const int MAX_ENCUESTAS[NUM_PAISES][NUM_SECTORES] = {

    { 1760,  1430,  2020,  1080,   290 },
    { 3850,  4240,  1670,  2860, 24020 },
    { 5440,  3100,  2000,  1560,  1000 }
};

typedef struct {
    int    idEncuesta;
    int    idPais;
    char   pais[3];
    int    idSector;
    double sentimiento;
} Encuesta;

typedef enum {
    MSG_CONEXION = 1,
    MSG_ENCUESTA = 2,
    MSG_FIN      = 3
} TipoMensaje;

typedef struct {
    TipoMensaje tipo;
    pid_t       pid;
    int         totalEncuestas;
    Encuesta    encuesta;
} Mensaje;

_Static_assert(sizeof(Mensaje) <= PIPE_BUF,
               "Mensaje debe ser <= PIPE_BUF para escrituras atomicas");

#define VALIDAR(llamada, condicionError, mensaje)                            \
    do {                                                                     \
        llamada;                                                             \
        if (condicionError) {                                                \
            perror(mensaje);                                                 \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

static inline int indicePais(const char *sigla)
{
    for (int i = 0; i < NUM_PAISES; i++)
        if (strcmp(sigla, SIGLA_PAIS[i]) == 0)
            return i;
    return -1;
}

static inline double calcularISE(const double promedio[NUM_SECTORES])
{
    double ise = 0.0;
    for (int s = 0; s < NUM_SECTORES; s++)
        ise += PESO_SECTOR[s] * promedio[s];
    return ise;
}

static inline const char *recomendacion(double ise)
{
    if (ise > UMBRAL_ISE) return "VENDER";
    if (ise < UMBRAL_ISE) return "COMPRAR";
    return "NEUTRO";
}

#endif
