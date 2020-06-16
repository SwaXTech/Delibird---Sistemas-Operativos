/*
 * conexiones.c
 *
 *  Created on: 2 mar. 2019
 *      Author: utnso
 */

#include "conexionesTeam.h"
uint32_t process_id;


//////////////////////////////////////////////
//				SUSCRIPCIONES				//
//////////////////////////////////////////////

void suscribirseAlBroker(void) {
	// Abro conexion
	int conexiones[3];

	conexiones[0] = abrirUnaConexion(config);
	suscribirAUnaCola(conexiones[0], APPEARED_POKEMON, process_id);

	conexiones[1] = abrirUnaConexion(config);
	suscribirAUnaCola(conexiones[1], LOCALIZED_POKEMON, process_id);

	conexiones[2] = abrirUnaConexion(config);
	suscribirAUnaCola(conexiones[2], CAUGHT_POKEMON, process_id);


	pthread_t thread1, thread2, thread3;
	pthread_create(&thread1, NULL, escucharAlSocket, &conexiones[0]);
	pthread_create(&thread2, NULL, escucharAlSocket, &conexiones[1]);
	pthread_create(&thread3, NULL, escucharAlSocket, &conexiones[2]);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	pthread_join(thread3, NULL);

//	for(int i = 0; i < 3; i++){
//		pthread_t thread;
//		log_debug(logger, "Escuchando en %d", conexiones[i]);
//		pthread_create(&thread, NULL, escucharAlSocket, &conexiones[i]); // Hilo para escuchar al broker
//		//pthread_detach(thread);
//		pthread_join(thread, NULL);
//	}


	return;
}

int abrirUnaConexion(t_config* config) {
	int conexion = crear_conexion_con_config(config, "IP_BROKER", "PUERTO_BROKER");
	if(conexion == CANT_CONNECT){
		log_debug(logger, "No pude conectar :(");
		terminar_programa(conexion, logger, config);
	} else {
		log_debug(logger, "Conexión Abierta");
	}
	return conexion;
}

void suscribirAUnaCola(int conexion, message_type cola, uint32_t process_id){

	// Creo el mensaje de subscripcion
	t_subscribe* subscripcion = subscribe(cola, process_id);

	uint32_t subscripcion_size;
	void* serialized_subscribe = serializarSubscribe(subscripcion, &subscripcion_size);

	uint32_t paquete_size;
	void* paquete_serializado = crear_paquete(SUBSCRIBE, serialized_subscribe, subscripcion_size, &paquete_size);

	//TODO: Handlear error
	send(conexion, paquete_serializado, paquete_size, 0);
	free(subscripcion);
	log_debug(logger, "Me suscribí a %d", cola);
}


void *escucharAlSocket(void* socket) {
	int i = 1;
	log_debug(logger, "Escuchando en el socket: %d", *((int*)socket));
	while(i) {	// TODO: PONER QUE EL WHILE SEA MIENTRAS NO ESTA EN EXIT
		t_paquete* paquete = recibirPaquete(*((int*)socket));

		if(paquete != NULL){
			enviarACK(paquete -> id);

			switch(paquete->type) {
				case ID:
					procesarID(paquete);
					break;

				case APPEARED_POKEMON:
					procesarAppeared(paquete);
					break;
				case LOCALIZED_POKEMON:
					// procesarLocalized(paquete); TODO
					log_debug(logger, "Que Google Maps ni Google Maps!. Localized Pokemon PAPÁ");
					break;
				case CAUGHT_POKEMON:
					procesarCaughtPokemon(paquete);

					break;
				default:
					log_debug(logger, "What is this SHIT?.");
					break;
			}


		} else {
			// Políticas de reconexión
			close(*((int*)socket));
			i = 0;
		}
	}
	// TODO DESTRUIR EL HILO?
	return NULL;
}

void procesarCaughtPokemon(t_paquete* paquete){
	t_caught_pokemon* cau_pok = deserializarCaughtPokemon(paquete -> buffer);

	if(*cau_pok == YES){
		log_debug(logger, "Yey! Atrapé un Pokemon!");
	} else if(*cau_pok == NO){
		log_debug(logger, "Ufa! No pude atraparlo :(");
	} else {
		log_debug(logger, "No entiendo man %d o %d o %d", *cau_pok, cau_pok, &cau_pok);
	}
}

void procesarAppeared(t_paquete* paquete){
	t_appeared_pokemon* pok = deserializarAppearedPokemon(paquete -> buffer);
	log_debug(logger, "Wow! Apareció un Pokemon: %s!", pok -> pokemon -> name);
}

void procesarID(t_paquete* paquete){
	t_id* id = paquete -> buffer -> stream;
	log_debug(logger, "Recibí el ID: %d", id);
}


void escucharAlGameboy(){

	pthread_t thread;
	pthread_create(&thread, NULL ,abrirSocketParaGameboy, NULL);
	pthread_detach(&thread);
}

void* abrirSocketParaGameboy(){

	char* ip = config_get_string_value(config, "IP");
	char* puerto = config_get_string_value(config, "PUERTO");
	log_debug(logger, "Estoy escuchando al gameboy en %s:%s", ip, puerto);
	crear_servidor(ip, puerto, serve_client);

}

void serve_client(int* socket){
	message_type type = recibirCodigoDeOperacion(*socket);
	if(type != NULL){
		log_debug(logger, "Procesando solicitud");
		process_request(type, *socket);
	}else {
		log_debug(logger, "No puedo procesar la solicitud");
	}
}

void process_request(message_type type, int socket){

	t_paquete* paquete = recibirPaqueteSi(socket, type);

	switch(type){

		case APPEARED_POKEMON:
			procesarAppeared(paquete);
			break;

		case CAUGHT_POKEMON:
			procesarCaughtPokemon(paquete);
			break;

		default:
			log_error(logger, "Código de operación inválido");


	}

}

void enviarACK(uint32_t id){

	int conexion = abrirUnaConexion(config);

	log_debug(logger,"Enviaré un ACK por el id: %d",id);
	t_ack* _ack = ack(process_id, id);

	uint32_t bytes_ack;
	void* serialized_ack = serializarACK(_ack, &bytes_ack);

	uint32_t bytes;
	void* a_enviar = crear_paquete(ACK, serialized_ack, bytes_ack, &bytes);

	int status = send(conexion, a_enviar, bytes, 0);
	log_debug(logger, "Envié un ACK al ID: %d, con status: %d", id, status);
	free(a_enviar);
	close(conexion);


}


//////////////////////////////////////////////
//					GET						//
//////////////////////////////////////////////

void enviarGetPokemon(t_pokemon* pokemon) {
	int conexion = crear_conexion_con_config(config, "IP_BROKER", "PUERTO_BROKER");

	uint32_t get_pokemon_size;
	void* serialized_get_pokemon = serializarPokemon(pokemon, &get_pokemon_size);

	uint32_t paquete_size;
	void* paquete_serializado = crear_paquete(GET_POKEMON, serialized_get_pokemon, get_pokemon_size, &paquete_size);

	int status = send(conexion, paquete_serializado, paquete_size, 0);

	// TODO ESperar el ID del mensaje

	liberar_conexion(conexion);

	free(paquete_serializado);
	return;
}


