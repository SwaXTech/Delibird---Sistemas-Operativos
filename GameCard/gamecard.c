/*
 * gamecard.c
 *
 *  Created on: 17 jun. 2020
 *      Author: utnso
 */

#include "gamecard.h"

t_log* logger = NULL;
t_config* config = NULL;
char* ruta_punto_montaje;

int main() {

	char* logfile;
	uint32_t tiempoReconexion;

	config = leer_config();

	logfile = config_get_string_value(config, "LOG_FILE");
	ruta_punto_montaje = config_get_string_value(config, "PUNTO_MONTAJE_TALLGRASS");
	tiempoReconexion = config_get_int_value(config, "TIEMPO_DE_REINTENTO_CONEXION");
	process_id = config_get_int_value(config, "PROCESS_ID");

	logger = iniciar_logger(logfile);

	//archivo_en_uso("/home/utnso/Escritorio/tall-grass/Files/");
	//verificar_pokemon("/home/utnso/Escritorio/tall-grass/Files/", "Charizard");
	t_coords* coordenadas;

	uint32_t x = coordenadas->posX;
	uint32_t y = coordenadas->posY;

	x = 4;
	y = 2;


	verificar_posiciones("/home/utnso/Escritorio/tall-grass/Blocks/1.bin",coordenadas);


	while(1) {
		if(abrirUnaConexionGameCard(config) == CANT_CONNECT)
			intentarReconexionConBroker(logfile, tiempoReconexion,  config);
		else {
			inicializar_gamecard();
			break;
		}
	}
	return 1;
}


void intentarReconexionConBroker(char* logfile, uint32_t tiempo, t_config* config){
	log_debug(logger, "Intentando nueva conexión en %d segundos", tiempo);
	sleep(tiempo);
}

void inicializar_gamecard(void) {
	escucharAlGameboy();
	suscribirGameCardAlBroker();
}

t_log* iniciar_logger(char* logfile) {
	return log_create(logfile, "GameCard", true, LOG_LEVEL_DEBUG);
}

t_config* leer_config(void) {
	return config_create("gamecard.config");
}

